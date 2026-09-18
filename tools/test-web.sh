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

# The direction of the dependency, which is the whole reason these are separate
# packages and not separate directories.
#
# npm's workspaces put every package in node_modules, so node would happily
# resolve any of these; this is what says no.
#
# Verified by mutation: each of the three turns red when the import it forbids
# is added to a file it covers.
fail=0

# 1. @webos/api is the platform: the bus, the card's own life, translation. It
#    must not know there is a screen, or an app could not be written for this
#    device in anything but our kit.
#    Code, not prose: the package's README says out loud that the kit depends on
#    it, and saying so is the point.
#    Matched inside quotes, which is what an import specifier looks like in
#    every form of it -- `from "..."`, `import "..."`, `import("...")`. Prose
#    may name the kit and does: connect-card.ts explains where the other half
#    lives, and that sentence is worth more than the grep is strict.
leak=$(cd "$ROOT" && grep -rlE --include='*.ts' '["'"'"']@webos/ui-kit|["'"'"']@gachlab/' sdk/webos-api 2>/dev/null | tr '\n' ' ')
if [ -n "$leak" ]; then
    echo "FAIL: @webos/api reaches up into the kit or an app: $leak"
    fail=1
fi

# 2. An app's logic is the layer a card written in something else would reuse,
#    so it answers to the same rule as the platform. Only main.ts -- the file
#    that draws -- may name the kit. This is the half the old "src/lib never
#    imports src/ui" covered and that the split nearly dropped: those services
#    used to live under src/lib and now live in their app.
leak=$(cd "$ROOT" && grep -rlE --include='*.service.ts' --include='luna/*.ts' '["'"'"']@webos/ui-kit' apps/*/src 2>/dev/null | tr '\n' ' ')
if [ -n "$leak" ]; then
    echo "FAIL: an app's logic reaches for the kit: $leak"
    fail=1
fi

# 3. And nobody steps around the package boundary with a relative path.
#    "resolution rather than discipline" is only true while every crossing is a
#    bare specifier: tsconfig.web.json is one program over all four packages, so
#    ../../../ui-kit/src/element.ts would type-check and the greps above would
#    never see it. Nothing legitimate in this tree needs three levels up -- the
#    deepest is sdk/webos-api/src/infra/luna, which reaches ../../helpers.
leak=$(cd "$ROOT" && grep -rn --include='*.ts' '"\.\./\.\./\.\./' sdk apps 2>/dev/null | tr '\n' ' ')
if [ -n "$leak" ]; then
    echo "FAIL: a package boundary crossed by relative path: $leak"
    fail=1
fi

# 4. And the example card stays an example. apps/example-plain exists to prove
#    that a card can be written without our renderer (#65), so the day someone
#    reaches for startCard or defineElement in it "to save a few lines", the
#    proof quietly stops proving anything. Importing the kit's ELEMENTS is what
#    it is meant to do; importing the machinery that draws them is not.
#
#    Verified by mutation: an import of @webos/ui-kit/start-card.ts there turns
#    this red.
leak=$(cd "$ROOT" && grep -rlE --include='*.ts' '["'"'"']@webos/ui-kit/(start-card|element)' apps/example-plain 2>/dev/null | tr '\n' ' ')
if [ -n "$leak" ]; then
    echo "FAIL: the card that proves the runtime is optional reaches for it: $leak"
    fail=1
fi

[ "$fail" -eq 0 ] || exit 1
echo "the layers hold, and the runtime is still optional          ok"

if [ $# -eq 0 ]; then
    set -- "$ROOT"/sdk/*/test/*.test.ts "$ROOT"/apps/*/test/*.test.ts
fi

exec node --test --test-timeout=60000 --experimental-transform-types "$@"
