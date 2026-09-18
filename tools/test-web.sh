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

# The direction of the dependency, which is the whole reason these are three
# packages and not three directories. @webos/api is the platform: the bus, the
# card's own life, translation. It must not know there is a screen, or an app
# could not be written for this device in anything but our kit.
#
# npm's workspaces put every package in node_modules, so node would happily
# resolve a wrong import; this is what says no.
#
# Verified by mutation: an import of @webos/ui-kit added to any file under
# sdk/webos-api turns this red.
leak=$(cd "$ROOT" && grep -rl '@webos/ui-kit\|@webos/app-' sdk/webos-api 2>/dev/null | tr '\n' ' ')
if [ -n "$leak" ]; then
    echo "FAIL: @webos/api reaches up into the kit or an app: $leak"
    exit 1
fi
echo "the platform package knows nothing about the kit                ok"

if [ $# -eq 0 ]; then
    set -- "$ROOT"/sdk/*/test/*.test.ts "$ROOT"/apps/*/test/*.test.ts
fi

exec node --test --test-timeout=60000 --experimental-transform-types "$@"
