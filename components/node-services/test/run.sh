#!/bin/bash
# Type-checks components/node-services and runs its tests on a private bus.
#
#   components/node-services/test/run.sh [test files...]
#
# The tests start their own ls-hubd, whose sockets have fixed paths under /tmp.
# So the run needs a /tmp of its own: a namespace from bwrap when there is one,
# or the container's own /tmp in CI (root, no user namespaces, nothing else on
# the bus). Anywhere else it skips rather than touch a running session's hub.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
COMPONENT="$(cd "$HERE/.." && pwd)"
ROOT="$(cd "$COMPONENT/../.." && pwd)"

. "$ROOT/tools/node-home.sh" \
    || { echo "SKIP: the pinned node is not unpacked (run tools/fetch-node.sh)"; exit 0; }

STAGING="$ROOT/build/staging"
PALMBUS="$ROOT/build/rootfs/usr/palm/nodejs/palmbus.node"
[ -x "$STAGING/usr/sbin/ls-hubd" ] || { echo "SKIP: ls-hubd is not staged"; exit 0; }
[ -f "$PALMBUS" ] || { echo "SKIP: palmbus.node is not built"; exit 0; }

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
export WEBOS_PALMBUS="$PALMBUS"
export LD_LIBRARY_PATH="$STAGING/usr/lib:$STAGING/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# --test-force-exit: palmbus keeps node's loop alive for as long as it is
# loaded, the way a service wants it, so a finished test file would never exit.
RUN=(node --test --test-force-exit --test-concurrency=1 "$@")

if bwrap --dev-bind / / --tmpfs /tmp true 2>/dev/null; then
    exec bwrap --dev-bind / / --tmpfs /tmp --die-with-parent "${RUN[@]}"
elif [ "$(id -u)" = 0 ] && [ ! -e /tmp/com.palm.private_hub ]; then
    exec "${RUN[@]}"
fi
echo "SKIP: no private /tmp for a test hub (bwrap unavailable, and not a throwaway container)"
exit 0
