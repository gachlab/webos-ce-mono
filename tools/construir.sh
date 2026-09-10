#!/bin/bash
# Construye webOS completo sobre un Debian moderno, en el orden del MANIFEST.
#
# El orden NO es invento nuestro: es el de build-webos-desktop.sh de HP, que ya
# venia ordenado topologicamente. MANIFEST.tsv lo conserva en su columna 1.
#
# Uso:
#   tools/construir.sh              # todo
#   tools/construir.sh cmake        # una etapa: autotools | cmake | qmake | rootfs
set -u
R="$(cd "$(dirname "$0")/.." && pwd)"
ETAPA="${1:-todo}"

# Componentes que el MANIFEST marca como cmake/qmake pero que NO construimos, y
# por que. Se listan aqui en vez de borrarlos del MANIFEST para que el manifiesto
# siga siendo el inventario fiel de lo que libero HP.
declare -A OMITIR=(
    [cmake]="es la herramienta; usamos la del sistema"
    [cmake-modules-webos]="son modulos CMake, se consumen por CMAKE_MODULE_PATH"
    [qt4]="reemplazado por el Qt5 del sistema"
    [webkit]="reemplazado por QtWebKit 5.212 en third-party/"
    [nodejs]="usamos el node de Debian; el de HP pide Python 2 y SCons"
    [nodejs-module-webos-sysbus]="addon en API v8 vieja, falta portarlo a N-API"
    [nodejs-module-webos-pmlog]="idem"
    [nodejs-module-webos-dynaload]="idem"
    [WebKitSupplemental]="camino del navegador (NPAPI), pendiente"
    [AdapterBase]="camino del navegador (NPAPI), pendiente"
    [BrowserServer]="camino del navegador (NPAPI), pendiente"
    [BrowserAdapter]="camino del navegador (NPAPI), pendiente"
    # OJO: db8 configura y compila sin leveldb, y Debian no lo empaqueta. Falta
    # confirmar en ejecucion si mojodb-luna necesita ese backend o trae otro.
    [leveldb]="db8 compila sin el; pendiente verificar en ejecucion"
)

lista() {  # lista <sistema-de-build> -> nombres en orden del MANIFEST
    awk -F'\t' -v s="$1" 'NR>1 && $5==s {print $2}' "$R/MANIFEST.tsv"
}

seleccion() {
    for c in $(lista "$1"); do
        [ -n "${OMITIR[$c]:-}" ] && continue
        echo "$c"
    done
}

etapa_autotools() {
    echo "== autotools =="
    for c in $(seleccion autotools); do
        d=$R/build-modern/$c
        mkdir -p "$d"; cd "$d" || { echo "$c: sin directorio"; return 1; }
        # cjson trae autogen.sh; el arbol de HP no incluye ./configure generado.
        [ -x "$R/components/$c/configure" ] || (cd "$R/components/$c" && ./autogen.sh >/dev/null 2>&1)
        if ! "$R/components/$c/configure" --prefix="$R/build-modern/staging" > cfg.log 2>&1 \
           || ! make -j"$(nproc)" > build.log 2>&1 || ! make install > install.log 2>&1; then
            printf "%-22s FALLA %s\n" "$c" "$(grep -m1 -iE 'error' build.log cfg.log 2>/dev/null | cut -c1-60)"
            return 1
        fi
        printf "%-22s OK\n" "$c"
    done
}

etapa_cmake() {
    echo "== CMake =="
    # shellcheck disable=SC2046
    "$R/tools/probar-servicios.sh" $(seleccion cmake)
}

etapa_qmake() {
    echo "== qmake =="
    "$R/tools/construir-qmake.sh"
}

etapa_rootfs() {
    echo "== rootfs =="
    # Los componentes "copiar" del MANIFEST no se compilan: son JS, temas y
    # datos. armar-rootfs.sh los coloca junto a los binarios ya instalados.
    "$R/tools/armar-rootfs.sh"
}

case "$ETAPA" in
    autotools) etapa_autotools ;;
    cmake)  etapa_cmake ;;
    qmake)  etapa_qmake ;;
    rootfs) etapa_rootfs ;;
    todo)   etapa_autotools && etapa_cmake && etapa_qmake && etapa_rootfs
            echo
            echo "Listo. Para arrancar el shell:  tools/correr-lunasysmgr.sh" ;;
    *)      echo "etapa desconocida: $ETAPA (autotools | cmake | qmake | rootfs | todo)"; exit 2 ;;
esac
