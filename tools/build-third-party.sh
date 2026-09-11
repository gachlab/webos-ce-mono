#!/bin/bash
# Construye QtWebKit 5.212, que es la unica dependencia que no vive en el repo.
#
# Hace falta porque webappmanager y BrowserServer hablan por NPAPI, que es la
# frontera que Palm dibujo entre el shell y el motor web. QtWebEngine (Chromium)
# no expone NPAPI, asi que no sirve de reemplazo.
#
# No se vendorea: son cientos de MB que nadie va a editar. Se clona en la ref
# exacta y se le aplica nuestro parche de portabilidad (ver patches/).
#
# Uso: tools/build-third-party.sh
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
    # Sin --depth: hace falta poder resolver el sha exacto.
    git clone "$ORIGEN" "$SRC"
    git -C "$SRC" checkout --detach "$REF"
else
    echo "== ya existe $SRC, se reutiliza =="
    git -C "$SRC" rev-parse HEAD | grep -q "^$REF" || {
        echo "OJO: el arbol no esta en $REF. Se deja como esta." >&2
    }
fi

# El parche se aplica sobre el arbol limpio. Si ya esta aplicado, git lo dice y
# se sigue: no es un error re-ejecutar el script.
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
# -j6 y no nproc: enlazar WebCore se come varios GB por proceso.
ninja -C "$BUILD" -j"${JOBS:-6}"

echo "== instalando en $PREFIX =="
ninja -C "$BUILD" install

echo
echo "QtWebKit listo. Los .pri de modulo quedan en:"
echo "  $PREFIX/mkspecs/modules"
echo "build-qmake.sh los encuentra solo via QMAKEPATH."
