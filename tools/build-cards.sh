#!/bin/bash
# Builds the cards in apps/ and sdk/ into build/cards/<app id>/, ready for
# tools/assemble-rootfs.sh to install as webOS web apps.
#
#   tools/build-cards.sh [app id...]     # all of them when none is named
#
# A card is any directory with an appinfo.json and a main.ts next to it: the
# apps under apps/, and the kit's showcase, which lives inside sdk/ui-kit
# because it is the kit's documentation and must not be able to fall behind it.
# The id comes out of the appinfo.json rather than the directory name, so a
# directory can be called "wifi" and still install as com.gachlab.app.wifi.
#
# Each card is one esbuild bundle: its entry point, everything it imports, and
# lit-html, in one main.js next to the page. No network: esbuild and lit-html
# are pinned in the repository's package.json, which CI installs into its image.
#
#   WEBOS_CARDS_DEV=1 tools/build-cards.sh   # keeps the fake bus in, for a card
#                                            # opened in an ordinary browser
set -u
R="$(cd "$(dirname "$0")/.." && pwd)"
KIT="$R/sdk/ui-kit/src"
OUT="$R/build/cards"
ESBUILD="$R/node_modules/.bin/esbuild"
DEV="${WEBOS_CARDS_DEV:-}"

# esbuild is installed with --ignore-scripts in CI's image, which leaves its
# launcher a node script rather than the native binary -- so node has to be on
# the PATH here, exactly as tools/test-web.sh needs it.
. "$R/tools/node-home.sh" \
    || { echo "SKIP: the pinned node is not unpacked (run tools/fetch-node.sh)"; exit 77; }

[ -x "$ESBUILD" ] || { echo "SKIP: esbuild is not installed (npm ci)"; exit 77; }

# id -> source directory, for every card in the tree.
declare -A SRC=()
declare -A CLAIMED=()   # every id an app answers to: its own, and its aliases
while IFS= read -r info; do
    dir="$(dirname "$info")"
    [ -f "$dir/main.ts" ] || continue          # the enyo references are not ours to bundle
    id="$(sed -n 's/.*"id"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$info" | head -1)"
    [ -n "$id" ] || { echo "  $dir: appinfo.json has no id"; exit 1; }
    # Two cards under one id used to mean the second quietly replaced the first
    # here and in the rootfs. There is no manifest on our side of the tree the
    # way MANIFEST.tsv is on HP's, so this is where a collision gets caught --
    # and #63 is about to add a second id per app.
    [ -z "${CLAIMED[$id]:-}" ] || {
        echo "  $id is claimed by ${CLAIMED[$id]} and by $dir"; exit 1
    }
    SRC["$id"]="$dir"
    CLAIMED["$id"]="$dir"
    # And the ids it answers to besides its own (#63). They share one namespace
    # with the real ids -- an alias nobody can reach because another app is
    # installed under that id exactly is a silently dead redirection, which is
    # worse than a build that stops. ls-hubd does the same for bus names, at
    # run time; here it is caught before the device.
    while read -r alias; do
        [ -n "$alias" ] || continue
        # Listing your own id is harmless -- the exact match answers first --
        # so it is not a collision with yourself.
        [ "$alias" != "$id" ] || continue
        [ -z "${CLAIMED[$alias]:-}" ] || {
            echo "  $alias is claimed by ${CLAIMED[$alias]} and, as an alias, by $dir"; exit 1
        }
        CLAIMED["$alias"]="$dir"
    done < <(sed -n 's/.*"aliases"[[:space:]]*:[[:space:]]*\[\([^]]*\)\].*/\1/p' "$info" \
             | tr ',' '\n' | sed -n 's/.*"\([^"]*\)".*/\1/p')
done < <(find "$R/apps" "$R/sdk" -name appinfo.json -not -path '*/node_modules/*' | sort)

ids=("$@")
if [ ${#ids[@]} -eq 0 ]; then
    ids=("${!SRC[@]}")
    # A full build owns this directory, so a card that no longer exists under
    # that id goes. It is not tidiness: assemble-rootfs.sh installs every
    # directory it finds in here, and the id comes out of appinfo.json now --
    # so renaming an app used to leave the old one built AND installed, quietly
    # running beside the new one. build/ is never wiped between runs, which is
    # the same hazard drop_stale_cache() answers on the CMake side.
    for built in "$OUT"/*/; do
        [ -d "$built" ] || continue
        old_id="$(basename "$built")"
        [ -z "${SRC[$old_id]:-}" ] || continue
        echo "  $old_id: no card claims this id any more, removing"
        rm -rf "${built%/}"
    done
fi

status=0
for id in "${ids[@]}"; do
    src="${SRC[$id]:-}"
    [ -n "$src" ] || { echo "  $id: no card by that id"; status=1; continue; }
    dest="$OUT/$id"
    rm -rf "$dest"
    mkdir -p "$dest"
    # --bundle, and the .css files come in as text: the same stylesheet the page
    # links is what the elements' shadow roots adopt.
    # --sourcemap=external: the map is written but the bundle does not name it,
    # so a card installed without the map (assemble-rootfs.sh drops it) does not
    # ask the device for a file that is not there.
    if ! "$ESBUILD" "$src/main.ts" \
            --bundle --format=iife --loader:.css=text --minify \
            --define:WEBOS_CARDS_DEV="${DEV:+true}${DEV:-false}" \
            --target=chrome120 --sourcemap=external \
            --outfile="$dest/main.js" 2> "$dest/build.log"; then
        echo "  $id: FAILED"
        cat "$dest/build.log"
        status=1
        continue
    fi
    rm -f "$dest/build.log"
    cp -f "$src/index.html" "$src/appinfo.json" "$dest/"
    cp -f "$KIT/page.css" "$KIT/kit.css" \
          "$KIT/theme-enyo.css" "$KIT/theme-modern.css" "$dest/"
    # Theme images the enyo palette paints with (toolbar-light, group chrome).
    if [ -d "$KIT/images" ]; then
        mkdir -p "$dest/images"
        cp -f "$KIT/images"/* "$dest/images/" 2>/dev/null || true
    fi
    # Whatever else the card ships: its own stylesheet, icons, images.
    for extra in "$src"/*.css "$src"/*.png "$src"/*.jpg "$src"/images; do
        [ -e "$extra" ] && cp -rf "$extra" "$dest/"
    done
    echo "  $id: $(du -h "$dest/main.js" | cut -f1)"
done
exit $status
