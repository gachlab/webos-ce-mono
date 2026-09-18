#!/bin/bash
# The root of the tree says what is HP's and what is ours.
#
# `components/` holds HP's code and nothing else, and MANIFEST.tsv is the list
# of it -- the same list that fixes the build order. That was a convention for
# a while, and conventions erode: fourteen directories of ours had drifted in
# there, so nobody opening the repository could tell the code we may rewrite
# from the code we only patch. This turns the convention into something that
# fails.
#
# Ours live by what they are: adapters/ makes HP's code run here, services/ is
# what HP never released, sdk/ is what an app is written against, apps/ are the
# applications, reference/ is read and never built.
#
# Verified by mutation: `mkdir components/mine` turns this red, and so does
# deleting a component the MANIFEST lists.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
failures=0

check() {
    if [ "$2" = "$3" ]; then
        printf '%-56s %-28s ok\n' "$1" "$2"
    else
        printf '%-56s %-28s FAILED, wanted %s\n' "$1" "$2" "$3"
        failures=$((failures + 1))
    fi
}

[ -f "$ROOT/MANIFEST.tsv" ] || { echo "SKIP: no MANIFEST.tsv"; exit 77; }

manifest=$(tail -n +2 "$ROOT/MANIFEST.tsv" | cut -f2 | sort -u)
dirs=$(cd "$ROOT/components" && find . -maxdepth 1 -mindepth 1 -type d -printf '%f\n' | sort)

# Anything in components/ that the MANIFEST does not list is ours, and does not
# belong there.
intruders=$(comm -13 <(echo "$manifest") <(echo "$dirs") | tr '\n' ' ')
check "components/ holds only what MANIFEST.tsv lists" "${intruders:-none}" "none"

# The other direction is not an equality: four of HP's entries are built from
# somewhere else (cmake and leveldb are external, qt4 and webkit are replaced by
# the distribution's Qt and by QtWebEngine), so they have no directory. Any
# OTHER one missing means a component was lost.
missing=$(comm -23 <(echo "$manifest") <(echo "$dirs") \
          | grep -v -x -E 'cmake|leveldb|qt4|webkit' | tr '\n' ' ')
check "and every component it lists is still there" "${missing:-none}" "none"

# Ours are where they say they are. Each of these is a claim about the kind of
# thing inside, so an empty one means the claim stopped being true.
for d in adapters services sdk apps reference; do
    n=$(find "$ROOT/$d" -maxdepth 1 -mindepth 1 -type d 2>/dev/null | wc -l)
    check "$d/ exists and is not empty" "$([ "$n" -gt 0 ] && echo yes || echo no)" "yes"
done

# And none of ours is named after one of HP's, which is what would make the
# first check pass while the confusion came back.
ours=$(cd "$ROOT" && find adapters services sdk apps reference -maxdepth 2 -mindepth 1 -type d -printf '%f\n' 2>/dev/null | sort -u)
clash=$(comm -12 <(echo "$manifest") <(echo "$ours") | tr '\n' ' ')
check "and none of ours borrows a component's name" "${clash:-none}" "none"

exit $((failures == 0 ? 0 : 1))
