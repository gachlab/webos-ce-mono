#!/bin/bash
# Builds a single-file webOS CE AppImage.
#
#   tools/mkappimage.sh                 # build the .deb if needed, then pack it
#   tools/mkappimage.sh path/to.deb     # pack a package that already exists
#
# The AppImage lands in dist/.
#
# Why this wraps the .deb instead of building its own tree.
#
# The payload an AppImage needs is exactly what the package already contains: a
# self-contained tree under /opt/webos-ce, built inside the Ubuntu container so
# it links against that distribution's Qt rather than the host's. Reproducing
# that here would mean a second copy of a build pipeline whose comments run to
# a hundred lines, free to drift from the first. So this takes the package as
# its input and adds the three files an AppImage is: AppRun, a .desktop entry
# and an icon.
#
# That also means the two artifacts are the same bits by construction. A bug
# fixed in one cannot be present in the other.
set -u

R="$(cd "$(dirname "$0")/.." && pwd)"
DIST="$R/dist"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/webos-ce-appimage"
ARCH=x86_64

# Pinned to the release these were fetched and tested with. "continuous" is a
# moving tag, so naming it without a version means a build that worked today
# can fail tomorrow for a reason nothing in this repository changed.
TOOL_URL="https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$ARCH.AppImage"
RUNTIME_URL="https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-$ARCH"

DEB="${1:-}"
if [ -z "$DEB" ]; then
    # Build one if the tree has no package matching HEAD. mkdeb.sh refuses a
    # dirty tree on its own, which is the check that matters here too.
    SHA="$(git -C "$R" rev-parse --short HEAD)"
    DEB="$(ls -t "$DIST"/*"$SHA"_amd64.deb 2>/dev/null | head -1)"
    if [ -z "$DEB" ]; then
        echo "== no package for HEAD ($SHA); building one =="
        "$R/tools/mkdeb.sh" || exit 1
        DEB="$(ls -t "$DIST"/*"$SHA"_amd64.deb 2>/dev/null | head -1)"
    else
        echo "== using the package already built for HEAD: $(basename "$DEB") =="
    fi
fi

if [ ! -f "$DEB" ]; then
    echo "mkappimage: no such package: $DEB" >&2
    exit 2
fi

# The version comes out of the package rather than being recomputed, so the
# AppImage cannot claim a version its payload does not have.
VERSION="$(dpkg-deb --field "$DEB" Version 2>/dev/null)"
if [ -z "$VERSION" ]; then
    echo "mkappimage: $DEB has no Version field; is it a .deb?" >&2
    exit 2
fi

echo "== fetching the AppImage tooling =="
mkdir -p "$CACHE"
for pair in "appimagetool:$TOOL_URL" "runtime-$ARCH:$RUNTIME_URL"; do
    f="$CACHE/${pair%%:*}"
    url="${pair#*:}"
    if [ ! -s "$f" ]; then
        echo "  downloading $(basename "$f")"
        curl -sSfL --max-time 300 -o "$f" "$url" || {
            echo "mkappimage: could not download $url" >&2
            rm -f "$f"
            exit 1
        }
    fi
    chmod +x "$f"
done
echo "  appimagetool: $("$CACHE/appimagetool" --version 2>&1 | head -1)"

BUILD="$(mktemp -d)"
trap 'rm -rf "$BUILD"' EXIT
APPDIR="$BUILD/AppDir"

echo "== unpacking the payload =="
mkdir -p "$APPDIR"
dpkg-deb -x "$DEB" "$APPDIR" || exit 1
du -sh "$APPDIR" | sed 's/^/  /'

echo "== adding the AppImage parts =="
install -m 0755 "$R/tools/appimage/AppRun"          "$APPDIR/AppRun"
install -m 0644 "$R/tools/appimage/webos-ce.desktop" "$APPDIR/webos-ce.desktop"
install -m 0644 "$R/tools/appimage/webos-ce.png"     "$APPDIR/webos-ce.png"
ln -sf webos-ce.png "$APPDIR/.DirIcon"

# dpkg-deb -x restores the archive's root-owned, sometimes 1777 modes. Inside a
# squashfs none of that means anything -- it is read-only and owned by whoever
# mounts it -- but mksquashfs warns about each one, and the noise buries a real
# error. The writable var lives in the user's data directory at runtime; see
# the AppRun comment.
chmod -R u+rwX "$APPDIR" 2>/dev/null || true

OUT="$DIST/webos-ce_${VERSION}_$ARCH.AppImage"
mkdir -p "$DIST"
echo "== packing =="
if ! ARCH=$ARCH "$CACHE/appimagetool" --runtime-file "$CACHE/runtime-$ARCH" \
        "$APPDIR" "$OUT" > "$BUILD/pack.log" 2>&1; then
    echo "mkappimage: packing FAILED. What it said:" >&2
    tail -20 "$BUILD/pack.log" | sed 's/^/  /' >&2
    exit 1
fi

echo
echo "built: $OUT"
ls -lh "$OUT" | awk '{print "  size: " $5}'
echo "  version: $VERSION"
echo
echo "Run it with:   $OUT"
echo "It needs bubblewrap on the host; everything else is inside."
