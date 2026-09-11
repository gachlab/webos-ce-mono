#!/bin/bash
# Levanta LunaSysMgr en un Linux moderno, sin instalar nada en el sistema.
#
# Solo hace falta que exista /etc/palm como directorio vacio: es la unica ruta
# que Settings.cpp lleva clavada (kSettingsFile = "/etc/palm/luna.conf").
# Todo lo demas -- SystemPath, ApplicationPath, UiComponentsPath -- se configura
# desde luna.conf, y el rootfs se enlaza ahi dentro con bwrap, o sea que el
# sistema de verdad no se toca.
set -u
R="$(cd "$(dirname "$0")/.." && pwd)"
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
    exec bwrap --dev-bind / / --bind "$ROOTFS/etc/palm" /etc/palm \
      "$ROOTFS/usr/lib/luna/LunaSysMgr" "${@:2}"
    ;;
  stop)
    pkill -x LunaSysMgr; pkill -x ls-hubd; echo "detenido"
    ;;
esac
