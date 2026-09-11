#!/bin/bash
# Levanta LunaSysMgr en un Linux moderno, sin instalar nada en el sistema.
#
# Hay dos clases de ruta absoluta que webOS espera del dispositivo:
#
#  - Las que SON configurables (SystemPath, ApplicationPath, los Directories de
#    ls2...). armar-rootfs.sh las reescribe apuntando al rootfs local.
#  - Las que NO. /etc/palm/luna.conf esta clavada en Settings.cpp, y el HTML de
#    las apps carga enyo con src="/usr/palm/frameworks/enyo/...". Esas hay que
#    hacerlas existir de verdad, y para eso esta bwrap.
#
# /etc/palm se monta directo. /usr/palm no se puede: /usr viene del sistema de
# solo lectura y bwrap no puede crear ahi el punto de montaje. La salida es
# poner un tmpfs sobre /usr, volver a montar dentro todo lo que ya habia, y
# anadir /usr/palm. El sistema de verdad sigue sin tocarse.
set -u
R="$(cd "$(dirname "$0")/.." && pwd)"
SELF="$R/tools/$(basename "$0")"   # absoluta: abajo hay un --chdir
S="$R/build-modern/staging"
ROOTFS="$R/build-modern/rootfs"
# El de qtwebkit va tambien porque WebAppMgr enlaza contra nuestro QtWebKit
# 5.212, que no esta en el sistema. ls-hubd hereda este entorno y se lo pasa a
# WebAppMgr cuando lo arranca.
export LD_LIBRARY_PATH="$S/lib:$S/usr/lib:$S/qtwebkit/lib/x86_64-linux-gnu"
export DISPLAY="${DISPLAY:-:0}"
export QT_QPA_PLATFORM=xcb   # LunaSysMgr pide el plugin "palm", que era del Qt propio de HP

mkdir -p /tmp/webos/ls2 /tmp/webos/captures


case "${1:-run}" in
  bus)
    pkill -x ls-hubd 2>/dev/null; sleep 1
    "$R/build-modern/ls2/src/ls-hubd/ls-hubd" --conf "$ROOTFS/etc/ls2/ls-private.conf" >/tmp/webos/ls-priv.log 2>&1 &
    "$R/build-modern/ls2/src/ls-hubd/ls-hubd" --public --conf "$ROOTFS/etc/ls2/ls-public.conf" >/tmp/webos/ls-pub.log 2>&1 &
    sleep 2
    echo "ls-hubd: $(pgrep -xc ls-hubd) instancias"
    ;;
  run)
    # Entrar al namespace una vez y reentrar en el mismo script, para que todo
    # lo que se lance abajo lo herede (incluido WebAppMgr).
    # OJO: esto va SOLO aqui, nunca en "bus". ls-hubd tiene que quedarse fuera:
    # valida a cada cliente leyendo su /proc/<pid>/exe, y desde dentro de otro
    # namespace no casa -- LunaSysMgr muere con "Invalid permissions for (null)".
    if [ -z "${WEBOS_EN_NAMESPACE:-}" ]; then
        export WEBOS_EN_NAMESPACE=1
        rebind=()
        for d in /usr/*; do [ -e "$d" ] && rebind+=(--bind "$d" "$d"); done
        exec bwrap --dev-bind / / \
            --bind "$ROOTFS/etc/palm" /etc/palm \
            --tmpfs /usr "${rebind[@]}" \
            --bind "$ROOTFS/usr/palm" /usr/palm \
            --chdir "$ROOTFS" \
            "$SELF" "$@"
    fi

    # WebAppMgr NO se arranca por el bus: LunaSysMgr es el servidor IPC y
    # WebAppMgr el cliente que se conecta de vuelta, asi que tiene que existir
    # ya. HP lo hacia igual en run-luna-sysmgr.sh: LunaSysMgr, esperar, y
    # WebAppMgr detras. Los .service de ls2 solo sirven para el arranque por
    # demanda del dispositivo.
    "$ROOTFS/usr/lib/luna/LunaSysMgr" "${@:2}" &
    lsm=$!
    sleep 3
    "$ROOTFS/usr/lib/luna/WebAppMgr" > /tmp/webos/WebAppMgr.log 2>&1 &
    wait $lsm
    ;;
  stop)
    pkill -x LunaSysMgr; pkill -x WebAppMgr; pkill -x ls-hubd; echo "detenido"
    ;;
esac
