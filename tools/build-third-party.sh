#!/bin/bash
# Builds QtWebKit 5.212, the only dependency that does not live in this repo.
#
# It is needed because webappmanager and BrowserServer talk over NPAPI, the
# boundary Palm drew between the shell and the web engine. QtWebEngine
# (Chromium) does not expose NPAPI, so it is not a replacement.
#
# Not vendored: hundreds of MB nobody is going to edit. It is cloned at the
# exact ref and our portability patch is applied (see patches/).
#
# Usage: tools/build-third-party.sh
set -eu

R="$(cd "$(dirname "$0")/.." && pwd)"
TP="$R/build-modern/third-party"
SRC="$TP/qtwebkit"
BUILD="$TP/qtwebkit-build"
PREFIX="$R/build-modern/staging/qtwebkit"

ORIGEN="https://github.com/qtwebkit/qtwebkit.git"
REF="756e1c8f23dc2720471298281c421c0076d02df8"   # rama 5.212, 27-may-2024
PARCHE="$R/patches/qtwebkit-5.212-debian-sid.patch"

mkdir -p "$TP"

if [ ! -d "$SRC/.git" ]; then
    echo "== clonando QtWebKit en $REF (unos 500 MB) =="
    # No --depth: we need to be able to resolve the exact sha.
    git clone "$ORIGEN" "$SRC"
    git -C "$SRC" checkout --detach "$REF"
else
    echo "== $SRC already exists, reusing it =="
    git -C "$SRC" rev-parse HEAD | grep -q "^$REF" || {
        echo "OJO: el arbol no esta en $REF. Se deja como esta." >&2
    }
fi

# The patch applies to a clean tree. If it is already applied, git says so and
# we carry on: re-running the script is not an error.
if git -C "$SRC" apply --check "$PARCHE" 2>/dev/null; then
    echo "== aplicando $PARCHE =="
    git -C "$SRC" apply "$PARCHE"
elif git -C "$SRC" apply --reverse --check "$PARCHE" 2>/dev/null; then
    echo "== el parche ya estaba aplicado =="
else
    echo "ERROR: el parche no aplica ni esta aplicado. Revisa $PARCHE" >&2
    exit 1
fi

echo "== configurando =="
mkdir -p "$BUILD"
cmake -S "$SRC" -B "$BUILD" -G Ninja \
    -DPORT=Qt \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DENABLE_TOOLS=OFF \
    -DENABLE_API_TESTS=OFF

echo "== compilando (3293 objetos; tarda) =="
# -j6 rather than nproc: linking WebCore eats several GB per process.
ninja -C "$BUILD" -j"${JOBS:-6}"

echo "== instalando en $PREFIX =="
ninja -C "$BUILD" install

echo
echo "QtWebKit listo. Los .pri de modulo quedan en:"
echo "  $PREFIX/mkspecs/modules"
echo "build-qmake.sh los encuentra solo via QMAKEPATH."
