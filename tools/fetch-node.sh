#!/bin/bash
# Download the node pinned in tools/node-version, verify it, and unpack it.
#
#   tools/fetch-node.sh              into build/node-dist
#   tools/fetch-node.sh <directory>  anywhere else (the CI and package images)
#
# This is a prerequisite, in the same place as installing Qt with apt: it is
# the one step that needs the network. The build itself never does, and still
# runs with the network off in tools/ci.sh. Nothing is downloaded twice: a tree
# that already unpacked the pinned version is left alone.
#
# The tarball is refused unless its SHA-256 matches the value pinned in the
# repository -- not a SHASUMS256.txt fetched next to it, which a compromised
# mirror would simply serve to match.
set -euo pipefail

R="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=node-version
. "$R/tools/node-version"

DEST="${1:-$R/build/node-dist}"
NAME="node-$NODE_VERSION-linux-x64"
MIRROR="${NODE_MIRROR:-https://nodejs.org/dist}"

case "$(uname -m)" in
    x86_64) ;;
    *) echo "fetch-node: only linux-x64 is pinned; this is $(uname -m)" >&2; exit 2 ;;
esac

if [ -x "$DEST/$NAME/bin/node" ] && [ -e "$DEST/$NAME/.verified" ]; then
    ln -sfn "$NAME" "$DEST/current"
    echo "fetch-node: $NODE_VERSION already in $DEST"
    exit 0
fi

mkdir -p "$DEST"
TMP="$(mktemp -d "$DEST/.fetch.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

echo "fetch-node: downloading $NAME.tar.xz"
curl -fsSL --retry 3 -o "$TMP/$NAME.tar.xz" "$MIRROR/$NODE_VERSION/$NAME.tar.xz"

GOT="$(sha256sum "$TMP/$NAME.tar.xz" | cut -d' ' -f1)"
if [ "$GOT" != "$NODE_SHA256_LINUX_X64" ]; then
    echo "fetch-node: SHA-256 mismatch for $NAME.tar.xz" >&2
    echo "  pinned: $NODE_SHA256_LINUX_X64" >&2
    echo "  got:    $GOT" >&2
    exit 1
fi

rm -rf "$DEST/$NAME"
tar -xJf "$TMP/$NAME.tar.xz" -C "$DEST"
: > "$DEST/$NAME/.verified"
ln -sfn "$NAME" "$DEST/current"
echo "fetch-node: $NODE_VERSION verified and unpacked into $DEST/$NAME"
