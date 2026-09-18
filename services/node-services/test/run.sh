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

# No --test-force-exit: a test file must end on its own once it closes its bus
# handles. One that hangs has left something open, and the timeout says so.
RUN=(node --test --test-timeout=60000 --test-concurrency=1 "$@")

# Its own pid namespace too: a run that dies (a timeout, a crash, a debugger)
# takes the hubs and db8 it started with it, instead of leaving them running.
# ls-hubd reads callers from /proc, so /proc is the namespace's own.
BWRAP=(bwrap --dev-bind / / --tmpfs /tmp --unshare-pid --proc /proc --die-with-parent)
if "${BWRAP[@]}" true 2>/dev/null; then
    exec "${BWRAP[@]}" "${RUN[@]}"
elif [ "$(id -u)" = 0 ] && [ ! -e /tmp/com.palm.private_hub ]; then
    exec "${RUN[@]}"
fi
echo "SKIP: no private /tmp for a test hub (bwrap unavailable, and not a throwaway container)"
exit 77
