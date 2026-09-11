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
# Las fuentes Prelude viven en el rootfs, pero el codigo las busca en
# /usr/share/fonts, que dentro del namespace es la del sistema. Sin esto webOS
# dibuja con la fuente equivocada: 0 familias Prelude visibles, 59 en total.
# Con el fonts.conf del rootfs (que INCLUYE el del sistema y solo suma un
# directorio): 21 Prelude y 97 en total, sin perder ninguna de Debian.
export FONTCONFIG_FILE="$ROOTFS/etc/fonts.conf"
export DISPLAY="${DISPLAY:-:0}"
export QT_QPA_PLATFORM=xcb   # LunaSysMgr pide el plugin "palm", que era del Qt propio de HP

mkdir -p /tmp/webos/ls2 /tmp/webos/captures

# Entrar al namespace y reentrar en el mismo script, para que todo lo que se
# lance abajo lo herede. Lo usan "run" y "servicios": los dos necesitan ver las
# rutas que webOS lleva clavadas.
# OJO: nunca en "bus". ls-hubd tiene que quedarse fuera -- valida a cada cliente
# leyendo su /proc/<pid>/exe, y desde dentro de otro namespace no casa;
# LunaSysMgr muere con "Invalid permissions for (null)".
entrar_namespace() {
  if [ -z "${WEBOS_EN_NAMESPACE:-}" ]; then
      export WEBOS_EN_NAMESPACE=1
      # tmpfs sobre /usr y sobre /usr/lib, volviendo a montar dentro todo lo
      # que ya habia, para poder anadir dos rutas que webOS lleva clavadas:
      #   /usr/palm      -> el HTML de las apps carga enyo desde ahi
      #   /usr/lib/luna  -> IMEManager.cpp:41 busca ahi el teclado virtual, y
      #                     Settings.cpp:164 las posiciones del dock. Sin esto
      #                     no se puede escribir y el dock sale vacio.
      #   /var/luna      -> Settings.cpp y el launcher guardan ahi su estado
      #                     (preferences/launcher3, launchpoints). Sin esto
      #                     fallan 6 escrituras y el dock no recuerda nada.
      #   /var/palm      -> PendingApplications lo abre al arrancar.
      # Las demas rutas absolutas del codigo (/media/cryptofs, /media/internal,
      # /usr/plugins...) no existen ni en el rootfs: son solo del dispositivo.
      rebind=()
      for d in /usr/*;     do [ -e "$d" ] && rebind+=(--bind "$d" "$d"); done
      rebind+=(--tmpfs /usr/lib)
      for d in /usr/lib/*; do [ -e "$d" ] && rebind+=(--bind "$d" "$d"); done
      rebind+=(--tmpfs /var)
      for d in /var/*;     do [ -e "$d" ] && rebind+=(--bind "$d" "$d"); done
      exec bwrap --dev-bind / / \
          --bind "$ROOTFS/etc/palm" /etc/palm \
          --tmpfs /usr "${rebind[@]}" \
          --bind "$ROOTFS/usr/palm" /usr/palm \
          --bind "$ROOTFS/usr/lib/luna" /usr/lib/luna \
          --bind "$ROOTFS/var/luna" /var/luna \
          --bind "$ROOTFS/var/palm" /var/palm \
          --chdir "$ROOTFS" \
          "$SELF" "$@"
  fi
}



case "${1:-run}" in
  bus)
    pkill -x ls-hubd 2>/dev/null; sleep 1
    "$R/build-modern/ls2/src/ls-hubd/ls-hubd" --conf "$ROOTFS/etc/ls2/ls-private.conf" >/tmp/webos/ls-priv.log 2>&1 &
    "$R/build-modern/ls2/src/ls-hubd/ls-hubd" --public --conf "$ROOTFS/etc/ls2/ls-public.conf" >/tmp/webos/ls-pub.log 2>&1 &
    sleep 2
    echo "ls-hubd: $(pgrep -xc ls-hubd) instancias"
    ;;
  init)
    # Inicializacion de una sola vez, copiada del caso "init" de service-bus.sh
    # de HP. Carga en db8 los esquemas (kinds) y permisos que estan en
    # /etc/palm/db. Sin esto mojodb-luna arranca pero responde "kind not
    # registered" a todo, y las apps salen vacias.
    # configurator no se lanza a mano: ls-hubd lo arranca al recibir la llamada.
    entrar_namespace "$@"
    # configurator lo arrancamos nosotros, no ls-hubd. El hub vive FUERA del
    # namespace, asi que lo que el lance no ve /etc/palm/db/kinds y configurator
    # no encuentra nada que cargar.
    pkill -x configurator 2>/dev/null; sleep 1
    "$ROOTFS/usr/lib/luna/configurator" service > /tmp/webos/configurator.log 2>&1 &
    sleep 3
    LS="$R/build-modern/ls2/src/luna-send/luna-send"
    for t in '{"types":["dbkinds","filecache"]}' '{"types":["dbpermissions"]}' '{"types":["activities"]}'; do
        echo "configurator <- $t"
        timeout 60 "$LS" -n 1 palm://com.palm.configurator/run "$t" 2>&1 | head -2
    done
    pkill -x configurator 2>/dev/null
    ;;
  servicios)
    entrar_namespace "$@"
    # La lista es la de HP en service-bus.sh (STATIC_SERVICES): los que no se
    # arrancan bajo demanda desde el bus. Sin ellos el shell dibuja pero las
    # apps no tienen de donde leer -- de ahi los "Service does not exist:
    # com.palm.systemservice / com.palm.preferences" del log.
    pkill -x LunaSysService 2>/dev/null; pkill -x mojodb-luna 2>/dev/null
    pkill -x activitymanager 2>/dev/null; pkill -x filecache 2>/dev/null
    pkill -x LunaUniversalSearchMgr 2>/dev/null
    sleep 1
    L="$ROOTFS/usr/lib/luna"
    "$L/mojodb-luna" -c /etc/palm/mojodb.conf /var/db > /tmp/webos/mojodb.log 2>&1 &
    sleep 2
    for svc in LunaSysService filecache activitymanager LunaUniversalSearchMgr; do
        [ -x "$L/$svc" ] || { echo "$svc: sin binario"; continue; }
        "$L/$svc" > "/tmp/webos/$svc.log" 2>&1 &
        sleep 1
    done
    sleep 2
    for svc in mojodb-luna LunaSysService filecache activitymanager LunaUniversalSearchMgr; do
        printf "%-24s %s\n" "$svc" "$(pgrep -xc "$svc" 2>/dev/null | grep -q '^0$' && echo MUERTO || echo vivo)"
    done
    ;;
  run)
    # Entrar al namespace una vez y reentrar en el mismo script, para que todo
    # lo que se lance abajo lo herede (incluido WebAppMgr).
    # OJO: esto va SOLO aqui, nunca en "bus". ls-hubd tiene que quedarse fuera:
    # valida a cada cliente leyendo su /proc/<pid>/exe, y desde dentro de otro
    # namespace no casa -- LunaSysMgr muere con "Invalid permissions for (null)".
    entrar_namespace "$@"

    # WebAppMgr NO se arranca por el bus: LunaSysMgr es el servidor IPC y
    # WebAppMgr el cliente que se conecta de vuelta, asi que tiene que existir
    # ya. HP lo hacia igual en run-luna-sysmgr.sh: LunaSysMgr, esperar, y
    # WebAppMgr detras. Los .service de ls2 solo sirven para el arranque por
    # demanda del dispositivo.
    "$ROOTFS/usr/lib/luna/LunaSysMgr" "${@:2}" &
    lsm=$!
    # Esperar a que LunaSysMgr abra su socket IPC en vez de dormir a ciegas: con
    # los servicios arriba tarda mas en arrancar, y si WebAppMgr llega antes
    # muere con "Failed to connect to socket: Connection refused".
    for _ in $(seq 40); do [ -S /tmp/pipcserver.sysmgr ] && break; sleep 1; done
    sleep 1
    "$ROOTFS/usr/lib/luna/WebAppMgr" > /tmp/webos/WebAppMgr.log 2>&1 &
    wait $lsm
    ;;
  stop)
    pkill -x LunaSysMgr; pkill -x WebAppMgr
    for s in mojodb-luna LunaSysService filecache activitymanager LunaUniversalSearchMgr; do pkill -x "$s" 2>/dev/null; done
    pkill -x ls-hubd; echo "detenido"
    ;;
esac
