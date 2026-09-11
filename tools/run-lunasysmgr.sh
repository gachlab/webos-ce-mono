#!/bin/bash
# Brings LunaSysMgr up on a modern Linux without installing anything on the
# host.
#
# webOS expects two kinds of absolute path from the device:
#
#  - The ones that ARE configurable (SystemPath, ApplicationPath, ls2's
#    Directories...). assemble-rootfs.sh rewrites those to point at the local
#    rootfs.
#  - The ones that are NOT. /etc/palm/luna.conf is hardcoded in Settings.cpp,
#    and the apps' HTML loads enyo with src="/usr/palm/frameworks/enyo/...".
#    Those have to actually exist, which is what bwrap is for.
#
# /etc/palm mounts directly. /usr/palm cannot: /usr comes from the read-only
# host and bwrap cannot create the mountpoint there. The way out is a tmpfs
# over /usr, remounting inside everything that was already there, and adding
# /usr/palm. The real system is still left untouched.
set -u
R="$(cd "$(dirname "$0")/.." && pwd)"
SELF="$R/tools/$(basename "$0")"   # absolute: there is a --chdir below
S="$R/build-modern/staging"
ROOTFS="$R/build-modern/rootfs"

# Linux truncates a process's comm to 15 characters, so `pgrep -x` and
# `pkill -x` silently match nothing for a longer name -- LunaUniversalSearchMgr
# is 22, and for a whole session it was reported dead while thirteen copies of it
# were running, because the same truncation stopped pkill from reaping them too.
# Comparing argv[0] out of /proc is exact and has no length limit. A pgrep -f
# regex would need the path escaped, and anchoring it with $ would miss
# mojodb-luna, which is launched with arguments.
service_pids() {   # service_pids <absolute path to the binary>
    local target="$1" dir pid argv0
    for dir in /proc/[0-9]*; do
        pid="${dir#/proc/}"
        # A process can vanish between the glob and the read; the redirection
        # itself then fails, and bash reports that before 2>/dev/null applies.
        { IFS= read -r -d "" argv0 < "$dir/cmdline"; } 2>/dev/null || continue
        [ "$argv0" = "$target" ] && echo "$pid"
    done
    return 0
}

service_running() {
    [ -n "$(service_pids "$1")" ]
}

service_stop() {
    local pids
    pids="$(service_pids "$1")"
    [ -n "$pids" ] && kill $pids 2>/dev/null
    return 0
}
# The qtwebkit one is there too because WebAppMgr links against our QtWebKit
# 5.212, which is not on the host. ls-hubd inherits this environment and passes
# it on to WebAppMgr when it starts it.
export LD_LIBRARY_PATH="$S/lib:$S/usr/lib:$S/qtwebkit/lib/x86_64-linux-gnu"

# bootstrap-node.js calls process.setName and process.setArgs, which existed
# only in HP's patched node. --require supplies them without run-js-service or
# the launcher having to know, and is inherited by every node the hub starts.
export NODE_OPTIONS="--require /usr/palm/nodejs/webos-node-compat.js${NODE_OPTIONS:+ $NODE_OPTIONS}"
# The Prelude fonts live in the rootfs, but the code looks for them in
# /usr/share/fonts, which inside the namespace is the host's. Without this webOS
# draws with the wrong font: 0 Prelude families visible, 59 in total. With the
# rootfs fonts.conf (which INCLUDES the host's and only adds one directory):
# 21 Prelude and 97 in total, losing none of Debian's.
export FONTCONFIG_FILE="$ROOTFS/etc/fonts.conf"
export DISPLAY="${DISPLAY:-:0}"
export QT_QPA_PLATFORM=xcb   # LunaSysMgr asks for the "palm" plugin, which came with HP's own Qt

mkdir -p /tmp/webos/ls2 /tmp/webos/captures

# ls-hubd y luna-send se toman de staging, no del arbol de build. El arbol vive
# en build-modern/<componente>/ y ese nombre depende de como se llame el
# componente en el MANIFEST: la ruta que habia aqui era build-modern/ls2/, del
# layout viejo, y dejo de existir en cuanto se construyo desde cero. staging es
# la ubicacion estable.

# Enter the namespace and re-enter this same script, so everything launched
# below inherits it. Used by "run" and "servicios": both need to see the paths
# webOS has hardcoded.
# NOTE: never in "bus". ls-hubd has to stay outside -- it validates each client
# by reading its /proc/<pid>/exe, and from inside another namespace that does
# not match; LunaSysMgr dies with "Invalid permissions for (null)".
entrar_namespace() {
  if [ -z "${WEBOS_EN_NAMESPACE:-}" ]; then
      export WEBOS_EN_NAMESPACE=1
      # tmpfs over /usr and over /usr/lib, remounting inside everything that
      # was already there, so we can add the paths webOS has hardcoded:
      #   /usr/palm      -> the apps' HTML loads enyo from there
      #   /usr/lib/luna  -> IMEManager.cpp:41 looks there for the virtual
      #                     keyboard, and Settings.cpp:164 for the dock
      #                     positions. Without it you cannot type and the dock
      #                     comes up empty.
      #   /var/luna      -> Settings.cpp and the launcher store their state
      #                     there (preferences/launcher3, launchpoints).
      #                     Without it 6 writes fail and the dock remembers
      #                     nothing.
      #   /var/palm      -> PendingApplications opens it at startup.
      # The other absolute paths in the code (/media/cryptofs, /media/internal,
      # /usr/plugins...) do not exist in the rootfs either: device-only.
      rebind=()
      for d in /usr/*;     do [ -e "$d" ] && rebind+=(--bind "$d" "$d"); done
      rebind+=(--tmpfs /usr/lib)
      for d in /usr/lib/*; do [ -e "$d" ] && rebind+=(--bind "$d" "$d"); done
      # The node binary goes where HP's bus role says it lives. A bind, not a
      # symlink: ls-hubd identifies a caller through /proc/<pid>/exe.
      node_bind=()
      node_real="$(command -v node 2>/dev/null || true)"
      if [ -n "$node_real" ] && [ -e "$ROOTFS/usr/palm/nodejs/node" ]; then
          node_bind=(--bind "$node_real" /usr/palm/nodejs/node)
      fi
      rebind+=(--tmpfs /var)
      for d in /var/*;     do [ -e "$d" ] && rebind+=(--bind "$d" "$d"); done
      exec bwrap --dev-bind / / \
          --bind "$ROOTFS/etc/palm" /etc/palm \
          --tmpfs /usr "${rebind[@]}" \
          --bind "$ROOTFS/usr/palm" /usr/palm \
          --bind "$ROOTFS/usr/lib/luna" /usr/lib/luna \
          "${node_bind[@]}" \
          --bind "$ROOTFS/var/luna" /var/luna \
          --bind "$ROOTFS/var/palm" /var/palm \
          --chdir "$ROOTFS" \
          "$SELF" "$@"
  fi
}



case "${1:-run}" in
  bus)
    pkill -x ls-hubd 2>/dev/null; sleep 1
    "$S/usr/sbin/ls-hubd" --conf "$ROOTFS/etc/ls2/ls-private.conf" >/tmp/webos/ls-priv.log 2>&1 &
    "$S/usr/sbin/ls-hubd" --public --conf "$ROOTFS/etc/ls2/ls-public.conf" >/tmp/webos/ls-pub.log 2>&1 &
    sleep 2
    echo "ls-hubd: $(pgrep -xc ls-hubd) instancias"
    ;;
  init)
    # One-time initialisation, copied from the "init" case of HP's
    # service-bus.sh. Loads into db8 the schemas (kinds) and permissions that
    # live under /etc/palm/db. Without it mojodb-luna starts but answers "kind
    # not registered" to everything, and the apps come up empty.
    entrar_namespace "$@"
    # We start configurator ourselves, not ls-hubd. The hub lives OUTSIDE the
    # namespace, so whatever it launches cannot see /etc/palm/db/kinds and
    # configurator finds nothing to load.
    pkill -x configurator 2>/dev/null; sleep 1
    "$ROOTFS/usr/lib/luna/configurator" service > /tmp/webos/configurator.log 2>&1 &
    sleep 3
    LS="$S/usr/bin/luna-send"
    for t in '{"types":["dbkinds","filecache"]}' '{"types":["dbpermissions"]}' '{"types":["activities"]}'; do
        echo "configurator <- $t"
        timeout 60 "$LS" -n 1 palm://com.palm.configurator/run "$t" 2>&1 | head -2
    done
    pkill -x configurator 2>/dev/null
    ;;
  servicios)
    entrar_namespace "$@"
    # HP's own list, from service-bus.sh (STATIC_SERVICES): the ones the bus does
    # not start on demand. Without them the shell draws but the apps have nothing
    # to read from -- hence the "Service does not exist: com.palm.systemservice /
    # com.palm.preferences" lines in the log.
    L="$ROOTFS/usr/lib/luna"
    ALL_SERVICES="mojodb-luna LunaSysService filecache activitymanager LunaUniversalSearchMgr"
    for svc in $ALL_SERVICES; do service_stop "$L/$svc"; done
    sleep 1
    "$L/mojodb-luna" -c /etc/palm/mojodb.conf /var/db > /tmp/webos/mojodb.log 2>&1 &
    sleep 2
    for svc in LunaSysService filecache activitymanager LunaUniversalSearchMgr; do
        [ -x "$L/$svc" ] || { echo "$svc: no binary"; continue; }
        "$L/$svc" > "/tmp/webos/$svc.log" 2>&1 &
        sleep 1
    done
    sleep 2
    for svc in $ALL_SERVICES; do
        printf "%-24s %s\n" "$svc" "$(service_running "$L/$svc" && echo alive || echo DEAD)"
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
    for s in mojodb-luna LunaSysService filecache activitymanager LunaUniversalSearchMgr; do
        service_stop "$ROOTFS/usr/lib/luna/$s"
    done
    pkill -x ls-hubd; echo "detenido"
    ;;
esac
