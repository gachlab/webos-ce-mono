#!/bin/bash
# Construye los componentes qmake de webOS contra el Qt5 del sistema.
#
# Complementa a probar-servicios.sh, que solo sabe de CMake. Los proyectos
# qmake de HP necesitan tres cosas que no son obvias:
#
#  1. -after en el qmake. Los .pro de HP ASIGNAN INCLUDEPATH ("INCLUDEPATH =
#     $$VPATH"), no la extienden. Un "INCLUDEPATH +=" pasado antes se pierde
#     cuando el .pro reasigna; -after lo aplica al final.
#  2. make -j1. Los .pro declaran mal sus dependencias y el build NO es seguro
#     en paralelo: sale un .o truncado, y make no lo vuelve a generar porque
#     su fecha ya es mas nueva que la del fuente. Hay que borrarlo a mano.
#  3. LUNA_STAGING y ROOTFS. Los .pri de HP los leen del entorno. LUNA_STAGING
#     arma los -L y -rpath; ROOTFS es el destino de install de keyboard-efigs,
#     que no instala en staging sino junto a los datos de ejecucion. Sin
#     exportarlo, su "$$(ROOTFS)/usr/lib/luna" se resuelve a /usr/lib/luna.
#
# Uso: tools/construir-qmake.sh [componente...]   (sin argumentos: todos)
set -u

R="$(cd "$(dirname "$0")/.." && pwd)"
S=$R/build-modern/staging

export LUNA_STAGING=$S
export ROOTFS=${ROOTFS:-$R/build-modern/rootfs}
export PKG_CONFIG_PATH=$S/lib/pkgconfig:$S/usr/lib/pkgconfig:$S/usr/share/pkgconfig
export LD_LIBRARY_PATH=$S/lib:$S/usr/lib

QMAKE=${QMAKE:-/usr/lib/qt5/bin/qmake}

# El orden es el del MANIFEST, que a su vez es el del script de HP.
ORDEN="luna-sysmgr-common luna-sysmgr keyboard-efigs webappmanager"
declare -A PRO=(
    [luna-sysmgr-common]=sysmgr-common.pro
    [luna-sysmgr]=sysmgr.pro
    [keyboard-efigs]=keyboard-efigs.pro
    [webappmanager]=webappmgr.pro
)

INCS="$S/include $S/usr/include $S/usr/include/sysmgr-ipc \
      $S/include/luna-sysmgr-common $S/include/luna-service2 \
      $S/include/ime $S/include/webkit/npapi"

fallo=0
for c in ${@:-$ORDEN}; do
    d=$R/components/$c
    p=${PRO[$c]:-}
    if [ -z "$p" ]; then echo "$c: no es un componente qmake conocido"; fallo=1; continue; fi

    cd "$d" || { fallo=1; continue; }
    # Empezar limpio: un .o truncado de una corrida en paralelo sobrevive a make.
    rm -rf debug-x86 release-x86 .qmake.stash Makefile.Ubuntu*

    if ! "$QMAKE" "$p" -after \
            "INCLUDEPATH += $INCS" \
            "LIBS += -L$S/lib -L$S/usr/lib" > qmake.log 2>&1; then
        printf "%-22s QMAKE FALLA   %s\n" "$c" "$(grep -m1 -iE 'error|cannot' qmake.log | cut -c1-60)"
        fallo=1; continue
    fi

    if ! make -f Makefile.Ubuntu -j1 > build.log 2>&1; then
        printf "%-22s COMPILA FALLA %s\n" "$c" \
            "$(grep -m1 -E 'error:|undefined reference' build.log | sed 's|.*/||' | cut -c1-60)"
        fallo=1; continue
    fi

    if ! make -f Makefile.Ubuntu install > install.log 2>&1; then
        printf "%-22s INSTALA FALLA %s\n" "$c" "$(grep -m1 -iE 'cannot|error' install.log | cut -c1-60)"
        fallo=1; continue
    fi

    printf "%-22s OK\n" "$c"
done
exit $fallo
