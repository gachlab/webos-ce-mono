#!/bin/bash
# Puts this tree's own packages into node_modules, so an app can import
# @webos/api and @webos/ui-kit by name.
#
#   tools/link-workspaces.sh
#
# `npm install` does this by itself, and on a development machine that is what
# does it. CI never runs npm inside the tree: the image carries one
# node_modules, installed while the network was still on, and the source is
# symlinked to it. npm's own workspace links are relative -- @webos/api points
# at ../../sdk/webos-api -- which resolves against the image's directory rather
# than the source, so they land nowhere. These are absolute.
#
# Verified by mutation: removing the call in tools/ci.sh makes every card fail
# to bundle with "Could not resolve @webos/api".
set -u
R="$(cd "$(dirname "$0")/.." && pwd)"
MODULES="${1:-$R/node_modules}"

[ -d "$MODULES" ] || { echo "$MODULES does not exist"; exit 1; }

for pkg in "$R"/sdk/*/package.json "$R"/apps/*/package.json; do
    [ -f "$pkg" ] || continue
    dir="$(dirname "$pkg")"
    name=$(sed -n 's/.*"name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$pkg" | head -1)
    [ -n "$name" ] || { echo "$pkg has no name"; exit 1; }
    mkdir -p "$MODULES/$(dirname "$name")"
    ln -sfn "$dir" "$MODULES/$name"
    echo "  $name -> $dir"
done
