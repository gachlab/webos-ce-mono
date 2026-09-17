#!/bin/bash
# Type-checks components/cards and runs its tests.
#
#   components/cards/test/run.sh [test files...]
#
# No bus and no browser: everything here is the library, which is why it is a
# library. What a card looks like on a page is tests/template-card.cpp, which
# runs the built card in the engine WebAppMgr uses.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
COMPONENT="$(cd "$HERE/.." && pwd)"
ROOT="$(cd "$COMPONENT/../.." && pwd)"

. "$ROOT/tools/node-home.sh" \
    || { echo "SKIP: the pinned node is not unpacked (run tools/fetch-node.sh)"; exit 77; }

TSC="$ROOT/node_modules/.bin/tsc"
if [ -x "$TSC" ]; then
    "$TSC" -p "$COMPONENT" || { echo "FAIL: type check"; exit 1; }
else
    echo "SKIP type check: TypeScript is not installed (npm ci)"
fi

if [ $# -eq 0 ]; then
    set -- "$HERE"/*.test.ts
fi

exec node --test --test-timeout=60000 --experimental-transform-types "$@"
