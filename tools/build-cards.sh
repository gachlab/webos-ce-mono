#!/bin/bash
# Builds the cards in components/cards into build/cards/<app id>/, ready for
# tools/assemble-rootfs.sh to install as webOS web apps.
#
#   tools/build-cards.sh [app id...]     # all of them when none is named
#
# Each card is one esbuild bundle: its entry point, everything it imports, and
# lit-html, in one main.js next to the page. No network: esbuild and lit-html
# are pinned in the repository's package.json, which CI installs into its image.
#
#   WEBOS_CARDS_DEV=1 tools/build-cards.sh   # keeps the fake bus in, for a card
#                                            # opened in an ordinary browser
set -u
R="$(cd "$(dirname "$0")/.." && pwd)"
CARDS="$R/components/cards"
OUT="$R/build/cards"
ESBUILD="$R/node_modules/.bin/esbuild"
DEV="${WEBOS_CARDS_DEV:-}"

# esbuild is installed with --ignore-scripts in CI's image, which leaves its
# launcher a node script rather than the native binary -- so node has to be on
# the PATH here, exactly as components/cards/test/run.sh needs it.
. "$R/tools/node-home.sh" \
    || { echo "SKIP: the pinned node is not unpacked (run tools/fetch-node.sh)"; exit 77; }

[ -x "$ESBUILD" ] || { echo "SKIP: esbuild is not installed (npm ci)"; exit 77; }

ids=("$@")
if [ ${#ids[@]} -eq 0 ]; then
    ids=()
    for dir in "$CARDS"/src/cards/*/; do
        [ -f "$dir/appinfo.json" ] && ids+=("$(basename "$dir")")
    done
fi

status=0
for id in "${ids[@]}"; do
    src="$CARDS/src/cards/$id"
    [ -f "$src/appinfo.json" ] || { echo "  $id: no appinfo.json"; status=1; continue; }
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
    cp -f "$CARDS/src/ui/page.css" "$CARDS/src/ui/kit.css" \
          "$CARDS/src/ui/theme-enyo.css" "$CARDS/src/ui/theme-modern.css" "$dest/"
    # Whatever else the card ships: its own stylesheet, icons, images.
    for extra in "$src"/*.css "$src"/*.png "$src"/*.jpg "$src"/images; do
        [ -e "$extra" ] && cp -rf "$extra" "$dest/"
    done
    echo "  $id: $(du -h "$dest/main.js" | cut -f1)"
done
exit $status
