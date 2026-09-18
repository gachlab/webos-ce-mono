#!/bin/bash
# The QtWebKit compatibility layer is split by concern (#59).
#
# One file held the API, the injected scripts, the bridge scheme, embedded
# pages and input. A mistake in any of them was a mistake in all of them, and
# changing the engine later (#68) would mean reimplementing a single 2 400-line
# ball. The split is the layout; this check is what keeps it from drifting
# back into one file.
#
# Verified by mutation: renaming src/page.cpp turns this red, and so does
# putting `src/qtwebkit_compat.cpp` back as a source in CMakeLists.txt.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$ROOT/adapters/qtwebkit-compat"
failures=0

check() {
    if [ "$2" = "$3" ]; then
        printf '%-56s %-28s ok\n' "$1" "$2"
    else
        printf '%-56s %-28s FAILED, wanted %s\n' "$1" "$2" "$3"
        failures=$((failures + 1))
    fi
}

[ -d "$DIR/src" ] || { echo "SKIP: no adapters/qtwebkit-compat/src"; exit 77; }

# The six concerns the ticket named, each its own translation unit.
for f in scripts.cpp bridge-scheme.cpp page.cpp frame.cpp embedded.cpp input.cpp; do
    check "$f is present" "$([ -f "$DIR/src/$f" ] && echo yes || echo no)" "yes"
done

# The monolith is gone: keeping it beside the split would let a caller pick
# the wrong one, and the point of the split is that there is no longer one.
check "the monolith src/qtwebkit_compat.cpp is gone" \
    "$([ -e "$DIR/src/qtwebkit_compat.cpp" ] && echo present || echo gone)" "gone"

# CMake builds the six, and only the six. A seventh .cpp quietly reintroduces
# the ball; dropping one of the six leaves a concern unbuilt.
listed=$(grep -E '^\s*src/[a-z0-9_-]+\.cpp\s*$' "$DIR/CMakeLists.txt" | tr -d ' \t' | sort)
wanted=$(printf '%s\n' \
    src/bridge-scheme.cpp \
    src/embedded.cpp \
    src/frame.cpp \
    src/input.cpp \
    src/page.cpp \
    src/scripts.cpp | sort)
check "CMakeLists.txt lists exactly the six sources" \
    "$([ "$listed" = "$wanted" ] && echo yes || echo no)" "yes"

# And does not name the monolith as a source either.
check "CMakeLists.txt does not build the monolith" \
    "$(grep -c 'qtwebkit_compat\.cpp' "$DIR/CMakeLists.txt" || true)" "0"

exit $((failures == 0 ? 0 : 1))
