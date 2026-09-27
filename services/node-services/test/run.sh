#!/bin/bash
# Type-checks services/node-services and runs its tests on a private bus.
#
#   services/node-services/test/run.sh [test files...]
#
# The tests start their own ls-hubd, whose sockets have fixed paths under /tmp.
# So the run needs a /tmp of its own: a namespace from bwrap when there is one,
# or the container's own /tmp in CI (root, no user namespaces, nothing else on
# the bus). Anywhere else it skips rather than touch a running session's hub.
# A skip exits 77, which ctest reports as skipped rather than passed.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
COMPONENT="$(cd "$HERE/.." && pwd)"
ROOT="$(cd "$COMPONENT/../.." && pwd)"

. "$ROOT/tools/node-home.sh" \
    || { echo "SKIP: the pinned node is not unpacked (run tools/fetch-node.sh)"; exit 77; }

STAGING="$ROOT/build/staging"
LUNABUS="$ROOT/build/staging/usr/palm/nodejs/lunabus.node"
[ -x "$STAGING/usr/sbin/ls-hubd" ] || { echo "SKIP: ls-hubd is not staged"; exit 77; }
[ -f "$LUNABUS" ] || { echo "SKIP: lunabus.node is not built"; exit 77; }

TSC="$ROOT/node_modules/.bin/tsc"
if [ -x "$TSC" ]; then
    "$TSC" -p "$COMPONENT" || { echo "FAIL: type check"; exit 1; }
else
    echo "SKIP type check: TypeScript is not installed (npm ci)"
fi

if [ $# -eq 0 ]; then
    set -- "$HERE"/*.test.ts
fi

export WEBOS_STAGING="$STAGING"
export WEBOS_LUNABUS="$LUNABUS"
export LD_LIBRARY_PATH="$STAGING/usr/lib:$STAGING/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# Where a run leaves its output, so a failure that happens once in many runs
# (#61) is not lost the way the first one was. Under build/Testing next to
# ctest's own LastTest.log, and outside /tmp, which the run replaces with a
# tmpfs of its own: a path under /tmp would die with the namespace. Passed to
# the daemons through WEBOS_TEST_LOGS (hub.ts) and to node's TAP reporter, so
# the record says which test failed and whether it was a timeout or an
# assertion. Unless the caller already chose a place for the daemon logs.
ARTIFACTS="$ROOT/build/Testing/node-services/$(date +%Y%m%d-%H%M%S)-$$"
mkdir -p "$ARTIFACTS"
: "${WEBOS_TEST_LOGS:=$ARTIFACTS}"
export WEBOS_TEST_LOGS
TAP="$ARTIFACTS/tests.tap"

# No --test-force-exit: a test file must end on its own once it closes its bus
# handles. One that hangs has left something open, and the timeout says so.
# spec goes to the terminal as before; tap is the file a later reader greps for
# the failing test's name and its "not ok" line.
RUN=(node --test --test-timeout=60000 --test-concurrency=1
     --test-reporter=spec --test-reporter-destination=stdout
     --test-reporter=tap --test-reporter-destination="$TAP" "$@")

# Its own pid namespace too: a run that dies (a timeout, a crash, a debugger)
# takes the hubs and db8 it started with it, instead of leaving them running.
# ls-hubd reads callers from /proc, so /proc is the namespace's own.
# Not exec'd any more: the script stays alive to point at the artifacts and to
# pass the run's own exit code back to ctest.
BWRAP=(bwrap --dev-bind / / --tmpfs /tmp --unshare-pid --proc /proc --die-with-parent)
if "${BWRAP[@]}" true 2>/dev/null; then
    "${BWRAP[@]}" "${RUN[@]}"
    status=$?
elif [ "$(id -u)" = 0 ] && [ ! -e /tmp/com.palm.private_hub ]; then
    "${RUN[@]}"
    status=$?
else
    echo "SKIP: no private /tmp for a test hub (bwrap unavailable, and not a throwaway container)"
    exit 77
fi

if [ "$status" -ne 0 ]; then
    echo "FAIL: node-services run left its output under $ARTIFACTS"
    # The failing tests, straight from the TAP, so the reader need not scroll
    # the whole run to learn which one it was and whether it timed out. A nested
    # subtest's "not ok" is indented, so match it anywhere on the line.
    if [ -f "$TAP" ]; then
        grep -E "^[[:space:]]*not ok" "$TAP" | sed 's/^/  /'
    fi
fi
exit "$status"
