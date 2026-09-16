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
S="${WEBOS_STAGING:-$R/build/staging}"
ROOTFS="${WEBOS_ROOTFS:-$R/build/rootfs}"

# Where the bus binaries are run from. A package has no build/staging, so these
# have to be overridable -- and they have to agree with the WEBOS_BINDIR that
# tools/assemble-rootfs.sh baked into com.palm.lunasend.json: ls-hubd checks a
# caller's /proc/<pid>/exe against the exeName in that role file, so running a
# luna-send from a path the role does not name costs it every permission it has,
# silently. Same defaults as that script's.
WEBOS_BINDIR="${WEBOS_BINDIR:-$S/usr/bin}"
WEBOS_SBINDIR="${WEBOS_SBINDIR:-$S/usr/sbin}"

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
# ls-hubd inherits this environment and passes it on to whatever it starts,
# WebAppMgr and the JavaScript services included.
# Our libraries sit in different places depending on how the tree was built:
# build/staging/{lib,usr/lib} for a developer, <prefix>/usr/lib for an installed
# package. Every one that exists is named, staging first so a development run
# keeps the precedence it always had.
#
# This is NOT made redundant by the $ORIGIN rpaths our binaries carry, and an
# earlier version of this line assumed it was. The three node addons are loaded
# by node -- not our binary, carrying no rpath of ours -- and they have no rpath
# of their own, so palmbus.node finds libluna-service2 through this variable or
# not at all. Measured on the installed package: 1 missing library without it, 0
# with it. Guarding the export on a staging directory therefore stopped every
# JavaScript service in the package dead, with
#   Error: libluna-service2.so.3: cannot open shared object file
# and with it the profile account, which is what leaves calendar empty.
#
# The principled fix is an rpath on the addons themselves, which the node stage
# of tools/build.sh does not pass today; until it does, this is what carries
# them.
WEBOS_LIBPATH=""
for d in "$S/lib" "$S/usr/lib" "$ROOTFS/usr/lib"; do
    [ -d "$d" ] || continue
    WEBOS_LIBPATH="${WEBOS_LIBPATH:+$WEBOS_LIBPATH:}$d"
done
if [ -n "$WEBOS_LIBPATH" ]; then
    export LD_LIBRARY_PATH="${WEBOS_LIBPATH}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

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
# A debugger against the Chromium the apps actually run in. With
#   WEBOS_WAM_INSPECTOR=9222 tools/run-lunasysmgr.sh
# QtWebEngine opens the DevTools server on that port, and every card, dashboard
# and headless page shows up as a target: http://127.0.0.1:9222 in a browser,
# or /json from a script. Off unless asked for -- it is a debug channel.
# On by default now, because not having it has cost more than it saves. Twice in
# one session a measurement had to be abandoned for want of it: the border-image
# boxes and the right-click path could both be read straight out of the page, and
# neither could be, because the shell happened to be started without the port.
# Restarting to gain a debugger throws away the state that made the bug visible.
#
# WEBOS_WAM_INSPECTOR=0 (or empty) turns it off, for a run where nothing should
# be listening. It binds 127.0.0.1 only.
WEBOS_WAM_INSPECTOR="${WEBOS_WAM_INSPECTOR-9222}"
if [ -n "$WEBOS_WAM_INSPECTOR" ] && [ "$WEBOS_WAM_INSPECTOR" != 0 ]; then
    export QTWEBENGINE_REMOTE_DEBUGGING="$WEBOS_WAM_INSPECTOR"
fi

# LunaSysMgr asks for the "palm" plugin, which came with HP's own Qt, so some
# platform has to be named here. Which one is not load-bearing: there is no
# X11-specific code in the shell, WebAppMgr or luna-sysmgr-common.
#
# Wayland, because on a Wayland desktop it is the shorter path -- no XWayland
# copy -- and it was measured to keep hardware GL: the shell still holds
# /dev/dri/renderD128 open, the browser launches, cards maximize, touch and
# text-field focus all work.
#
# Debugging the UI from outside needs xcb, and the environment wins:
#   QT_QPA_PLATFORM=xcb tools/run-lunasysmgr.sh run
# Under Wayland the window is not an X client, so xdotool and ImageMagick's
# import cannot see it, and GNOME refuses org.gnome.Shell.Screenshot over D-Bus.
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-wayland}"

mkdir -p /tmp/webos/ls2 /tmp/webos/captures

# Whether this machine has wifi, as the shell asks the question.
#
# DeviceInfo decides m_wifiAvailable from whether luna-prefs can resolve
# "com.palm.properties.WIFIoADDR", and that answer gates everything wifi in the
# UI: StatusBarServicesConnector only subscribes to com.palm.wifi when it is
# true, and SystemMenu only shows the wifi entry. With it false the indicator
# cannot move no matter what answers the bus.
#
# lunaprefs.c resolves such a key by stripping "com.palm.properties." and looking
# for a FILE of that name in three places, in order: /etc/prefs/properties,
# /dev/tokens, and /tmp/misc-props. The first two are absolute paths on the host
# -- neither exists, and the namespace binds onto directories that already do, so
# neither can be created without touching the system this script promises not to
# touch. The third is in /tmp, which is the host's own /tmp inside the namespace,
# and is where a running system was always allowed to add properties.
#
# DeviceInfo reads the value into a variable it discards: what it tests is
# whether the key resolves at all. So this is a marker and says so, rather than
# an address that would go stale the moment the machine changed adapters -- the
# real name, address and signal come from com.palm.connectionmanager and
# com.palm.wifi, which read NetworkManager.
mkdir -p /tmp/misc-props
[ -e /tmp/misc-props/WIFIoADDR ] || echo "present" > /tmp/misc-props/WIFIoADDR

# ls-hubd and luna-send come from staging, not from a component's build
# directory. Those are named after the component in the MANIFEST, and the path
# that used to be here (build/ls2/) stopped existing the moment the project was
# built from zero. staging is the location that stays put.

# Enter the namespace and re-enter this same script, so everything launched
# below inherits it. Used by "run" and "services": both need to see the paths
# webOS has hardcoded.
# NOTE: never in "bus". ls-hubd has to stay outside -- it validates each client
# by reading its /proc/<pid>/exe, and from inside another namespace that does
# not match; LunaSysMgr dies with "Invalid permissions for (null)".
enter_namespace() {
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
      #   /var/db        -> db8's data. Without it mojodb-luna wrote into the
      #                     tmpfs over /var, so every restart of the services
      #                     emptied the database: the kinds init had loaded were
      #                     gone, every query answered "kind not registered",
      #                     and the apps came up empty until init was run again.
      # The other absolute paths in the code (/media/cryptofs, /media/internal,
      # /usr/plugins...) do not exist in the rootfs either: device-only.
      rebind=()
      for d in /usr/*;     do [ -e "$d" ] && rebind+=(--bind "$d" "$d"); done
      rebind+=(--tmpfs /usr/lib)
      for d in /usr/lib/*; do [ -e "$d" ] && rebind+=(--bind "$d" "$d"); done
      # node used to be bound in here from the host, over an empty file in the
      # rootfs. The rootfs carries the real binary now -- the one pinned in
      # tools/node-version, copied by assemble-rootfs.sh -- and it arrives with
      # the /usr/palm bind below like everything else in that directory.
      node_bind=()
      if [ -e "$ROOTFS/usr/lib/libmemcpy.so" ]; then
          node_bind+=(--bind "$ROOTFS/usr/lib/libmemcpy.so" /usr/lib/libmemcpy.so)
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
          --bind "$ROOTFS/var/db" /var/db \
          --chdir "$ROOTFS" \
          "$SELF" "$@"
  fi
}



case "${1:-run}" in
  bus)
    pkill -x ls-hubd 2>/dev/null; sleep 1
    "$WEBOS_SBINDIR/ls-hubd" --conf "$ROOTFS/etc/ls2/ls-private.conf" >/tmp/webos/ls-priv.log 2>&1 &
    "$WEBOS_SBINDIR/ls-hubd" --public --conf "$ROOTFS/etc/ls2/ls-public.conf" >/tmp/webos/ls-pub.log 2>&1 &
    sleep 2
    echo "ls-hubd: $(pgrep -xc ls-hubd) instances"
    ;;
  init)
    # One-time initialisation, copied from the "init" case of HP's
    # service-bus.sh. Loads into db8 the schemas (kinds) and permissions that
    # live under /etc/palm/db. Without it mojodb-luna starts but answers "kind
    # not registered" to everything, and the apps come up empty.
    enter_namespace "$@"
    # db8 has to be answering before configurator runs: it registers every kind
    # by calling com.palm.db. Run before the services were up, configurator
    # rejected all 35 of them with "com.palm.db is not running" and left the
    # database empty -- which looks like working apps with nothing in them: memos
    # opens empty and refuses to add, calendar retries getCalendars forever with
    # "kind not registered".
    L="$ROOTFS/usr/lib/luna"
    if ! service_running "$L/mojodb-luna"; then
        echo "db8 was not running; starting it"
        "$L/mojodb-luna" -c /etc/palm/mojodb.conf /var/db > /tmp/webos/mojodb.log 2>&1 &
    fi
    for _ in $(seq 30); do
        timeout 3 "$WEBOS_BINDIR/luna-send" -n 1 palm://com.palm.db/find '{"query":{"from":"com.palm.db.kind:1"}}' </dev/null 2>&1 \
            | grep -q 'is not running' || break
        sleep 1
    done

    # We start configurator ourselves, not ls-hubd. The hub lives OUTSIDE the
    # namespace, so whatever it launches cannot see /etc/palm/db/kinds and
    # configurator finds nothing to load.
    pkill -x configurator 2>/dev/null; sleep 1
    "$ROOTFS/usr/lib/luna/configurator" service > /tmp/webos/configurator.log 2>&1 &
    sleep 3
    LS="$WEBOS_BINDIR/luna-send"
    for t in '{"types":["dbkinds","filecache"]}' '{"types":["dbpermissions"]}' '{"types":["activities"]}'; do
        echo "configurator <- $t"
        timeout 60 "$LS" -n 1 palm://com.palm.configurator/run "$t" 2>&1 | head -2
    done
    pkill -x configurator 2>/dev/null

    # The profile account HP made when first use was skipped. Its upstart job,
    # com.palm.service.accounts/files/etc/event.d/createLocalAccount, called
    # this method once LunaSysMgr had started. There is no upstart here, so
    # nobody ever called it -- and without that account nothing provides the
    # CALENDAR capability, which is what left the calendar empty: the app
    # creates even its own local calendar only for an account whose templateId
    # is com.palm.palmprofile (app/shared/CalendarsManager.js, gotCalendars).
    # The handler asks listAccounts for an existing one before creating any, so
    # running this on every init is harmless.
    echo "accounts <- createLocalAccount"
    timeout 30 "$LS" -n 1 palm://com.palm.service.accounts/createLocalAccount '{}' 2>&1 | head -2
    ;;
  services)
    enter_namespace "$@"
    # HP's own list, from service-bus.sh (STATIC_SERVICES): the ones the bus does
    # not start on demand. Without them the shell draws but the apps have nothing
    # to read from -- hence the "Service does not exist: com.palm.systemservice /
    # com.palm.preferences" lines in the log.
    L="$ROOTFS/usr/lib/luna"
    ALL_SERVICES="mojodb-luna LunaSysService sysfs-powerd nm-connectionmanager filecache activitymanager LunaUniversalSearchMgr"
    for svc in $ALL_SERVICES; do service_stop "$L/$svc"; done
    sleep 1
    "$L/mojodb-luna" -c /etc/palm/mojodb.conf /var/db > /tmp/webos/mojodb.log 2>&1 &
    sleep 2
    for svc in LunaSysService sysfs-powerd nm-connectionmanager filecache activitymanager LunaUniversalSearchMgr; do
        [ -x "$L/$svc" ] || { echo "$svc: no binary"; continue; }
        "$L/$svc" > "/tmp/webos/$svc.log" 2>&1 &
        sleep 1
    done
    # HP's JavaScript services. They are not in STATIC_SERVICES because on a
    # device the hub starts them on demand -- which it cannot do here: ls-hubd
    # runs outside the bwrap namespace on purpose (it identifies callers through
    # /proc/<pid>/exe) and so cannot see /usr/palm/services. Starting them the
    # way the C++ ones are started is the same end result.
    #
    # -k disables the activity timeout, which otherwise makes a service quit a
    # few seconds after it stops being asked anything.
    sleep 2
    for svc in $ALL_SERVICES; do
        printf "%-24s %s\n" "$svc" "$(service_running "$L/$svc" && echo alive || echo DEAD)"
    done
    ;;
  ns-exec)
    # What ls-hubd runs to start a C++ service on demand. assemble-rootfs.sh
    # points every such .service file here.
    #
    # The hub lives outside the namespace, so whatever it launched directly saw
    # the host's filesystem, where /etc/palm and /var/db do not exist. That broke
    # init: configurator is started inside the namespace, but when it had not
    # registered yet the hub launched a copy of its own from outside, and that
    # copy answered -- "No configurations found in /etc/palm/db/kinds", 2
    # configurations instead of 41, and a db8 with no kinds in it. Going through
    # here, a service launched by the hub sees the same paths as one started by
    # this script.
    enter_namespace "$@"
    shift
    exec "$@"
    ;;
  js-service)
    # What ls-hubd runs when something calls a JavaScript service that is not up:
    # assemble-rootfs.sh points each such .service file here. The hub is outside
    # the namespace and the service has to be inside it, so this enters it and
    # hands over to HP's launcher unchanged.
    #
    # Through bash, because run-js-service is "#!/bin/sh" but written for the
    # device's shell: `[ $jail == on ]` is not POSIX, and dash rejects it with
    # "unexpected operator" on every launch.
    enter_namespace "$@"
    shift
    exec bash /usr/lib/luna/run-js-service "$@"
    ;;
  run)
    # Enter the namespace once and re-enter this same script, so everything
    # started below inherits it -- WebAppMgr included.
    #
    # This belongs here and nowhere else, and never in "bus": ls-hubd has to stay
    # outside. It identifies every caller by reading /proc/<pid>/exe, and from
    # inside another namespace that does not match -- LunaSysMgr then dies with
    # "Invalid permissions for (null)".
    enter_namespace "$@"

    # WebAppMgr is NOT started through the bus: LunaSysMgr is the IPC server and
    # WebAppMgr the client that connects back to it, so the server has to exist
    # already. HP did the same in run-luna-sysmgr.sh -- LunaSysMgr, wait, then
    # WebAppMgr. The ls2 .service files are only for on-demand starts on the
    # device.
    ${WEBOS_SYSMGR_WRAPPER:+"$WEBOS_SYSMGR_WRAPPER"} "$ROOTFS/usr/lib/luna/LunaSysMgr" "${@:2}" &
    lsm=$!
    # Wait for LunaSysMgr to open its IPC socket rather than sleeping blindly:
    # with the services up it takes longer to start, and a WebAppMgr that arrives
    # first dies with "Failed to connect to socket: Connection refused".
    for _ in $(seq 40); do [ -S /tmp/pipcserver.sysmgr ] && break; sleep 1; done
    sleep 1
    # WEBOS_WAM_WRAPPER names a program to start WebAppMgr through -- a script
    # that runs it under gdb, say. It is inside the namespace and gets the same
    # environment, so what it reports is what the shell sees.
    ${WEBOS_WAM_WRAPPER:+"$WEBOS_WAM_WRAPPER"} "$ROOTFS/usr/lib/luna/WebAppMgr" > /tmp/webos/WebAppMgr.log 2>&1 &
    wait $lsm
    ;;
  stop)
    pkill -x LunaSysMgr; pkill -x WebAppMgr
    for s in mojodb-luna LunaSysService sysfs-powerd nm-connectionmanager filecache activitymanager LunaUniversalSearchMgr; do
        service_stop "$ROOTFS/usr/lib/luna/$s"
    done
    # The JavaScript services too, and they cannot be found the way the C++ ones
    # are: service_pids compares argv[0] against a binary path, and every JS
    # service has the same argv[0] -- /usr/palm/nodejs/node -- which matches no
    # service path, so this loop never reaped one. They survived every restart
    # instead. One session ended with five copies of com.palm.service.accounts
    # and two of com.palm.connectionmanager alive at once, 236 minutes of
    # accumulated CPU between them, with the machine at 13.7% idle.
    #
    # What does identify them is the working directory run-js-service leaves
    # them in: the service's own directory under /usr/palm/services.
    for d in /proc/[0-9]*; do
        case "$(readlink "$d/cwd" 2>/dev/null)" in
            */usr/palm/services/*) kill "${d#/proc/}" 2>/dev/null ;;
        esac
    done
    pkill -x ls-hubd; echo "stopped"
    ;;
esac
