#!/bin/bash
# Type-checks everything on the web platform and runs its tests.
#
#   tools/test-web.sh [test files...]
#
# No bus and no browser: everything here is the library, which is why it is a
# library. What a card looks like on a page is tests/template-card.cpp, which
# runs the built card in the engine WebAppMgr uses.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

. "$ROOT/tools/node-home.sh" \
    || { echo "SKIP: the pinned node is not unpacked (run tools/fetch-node.sh)"; exit 77; }

TSC="$ROOT/node_modules/.bin/tsc"
if [ -x "$TSC" ]; then
    "$TSC" -p "$ROOT/tsconfig.web.json" || { echo "FAIL: type check"; exit 1; }
else
    echo "SKIP type check: TypeScript is not installed (npm ci)"
fi

# The rules that make this three layers and not three directories: what may
# import what, and that nobody steps around a package by relative path. It is a
# script rather than a grep because two of the greps it replaces could not fire
# -- see the comments in it.
node "$ROOT/tools/check-layers.mjs" || exit 1

if [ $# -eq 0 ]; then
    set -- "$ROOT"/sdk/*/test/*.test.ts "$ROOT"/apps/*/test/*.test.ts
fi

exec node --test --test-timeout=60000 --experimental-transform-types "$@"
