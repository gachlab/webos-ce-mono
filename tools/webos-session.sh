#!/bin/bash
# One process that owns the whole stack, for an installed webOS CE.
#
#   webos-session.sh            bring everything up and stay in the foreground
#   webos-session.sh --down     tear down whatever is running and exit
#
# Only one session runs at a time. A second start while one is up says so and
# exits without touching it; --down still works, since it is how to recover.
#
# webOS's own Power Off and Restart end up here too. com.palm.power
# (components/sysfs-powerd) writes "poweroff" or "restart" to
# $WEBOS_SESSION_REQUEST and ends the shell; after the shell exits, a restart
# brings the whole stack back and anything else stops. The machine itself is
# never powered off or rebooted.
#
# Why this exists, and why it is not tools/run-lunasysmgr.sh.
#
# run-lunasysmgr.sh is the development tool and stays exactly as it is: granular
# subcommands, nothing torn down behind your back, db8 left warm so restarting
# the shell costs seconds instead of a fresh init. That is the right shape when
# you are rebuilding LunaSysMgr twenty times an hour.
#
# It is the wrong shape for someone who installed a package. There, killing the
# shell leaves eight processes running -- two ls-hubd, mojodb-luna,
# LunaSysService, filecache, activitymanager, LunaUniversalSearchMgr and the
# JavaScript services -- plus a stale /tmp/pipcserver.sysmgr. Measured, not
# supposed. The `run` case ends in `wait` and the script has no trap at all, so
# when LunaSysMgr dies the script simply returns and nobody reaps anything.
#
# This is the supervisor: it starts the stack in order, waits on the shell, and
# tears everything down however it is asked to stop -- Ctrl-C, a TERM from a
# service manager, or the shell exiting on its own.
set -u

R="$(cd "$(dirname "$0")/.." && pwd)"
LAUNCH="${WEBOS_LAUNCHER:-$R/tools/run-lunasysmgr.sh}"
ROOTFS="${WEBOS_ROOTFS:-$R/build/rootfs}"
LOGDIR="${WEBOS_LOGDIR:-/tmp/webos}"
# Inherited by the services through the launcher, so com.palm.power writes
# where this reads.
export WEBOS_SESSION_REQUEST="$LOGDIR/session-request"

# init is idempotent -- configurator checks what is already registered and
# createLocalAccount asks listAccounts before creating anything -- but it costs
# the better part of a minute, which is not a price to pay on every start. The
# sentinel records that a tree has been initialised once.
SENTINEL="$ROOTFS/var/luna/.webos-initialised"

say() { printf '%s\n' "$*" >&2; }

# Everything this supervisor is responsible for, in reverse dependency order.
# The JavaScript services cannot be found the way the C++ ones are: they all run
# /usr/palm/nodejs/node, so argv[0] identifies none of them. Their working
# directory does.
teardown() {
    trap - EXIT INT TERM          # so a second signal does not re-enter this
    say "webos: shutting down"

    pkill -x LunaSysMgr 2>/dev/null
    pkill -x WebAppMgr  2>/dev/null

    # One sweep of /proc, not one per service name.
    #
    # Every readlink here is a fork. This machine runs about 460 processes, and
    # teardown runs twice per session -- once before starting, once from the EXIT
    # trap -- so the nested version cost roughly 3,200 forks and eleven seconds a
    # pass. Adding a seventh service was enough to push tests/session-lock past
    # its timeout, which is how the cost was noticed at all.
    #
    # Both kinds of service are recognised in the same pass. HP's JavaScript ones
    # all run the same node binary, so they are found by the working directory
    # run-js-service leaves them in; ours are found by their path, and never by
    # name alone -- "nm-connectionmanager" is 20 characters and Linux truncates
    # comm to 15, so anything pgrep -x based would silently miss it.
    services=" mojodb-luna LunaSysService sysfs-powerd nm-connectionmanager storaged filecache activitymanager LunaUniversalSearchMgr "
    for d in /proc/[0-9]*; do
        pid=${d#/proc/}
        exe="$(readlink "$d/exe" 2>/dev/null)"
        case "$exe" in
            "$ROOTFS/usr/lib/luna/"*)
                case "$services" in
                    *" ${exe##*/} "*) kill "$pid" 2>/dev/null ;;
                esac
                ;;
        esac
        case "$(readlink "$d/cwd" 2>/dev/null)" in
            */usr/palm/services/*) kill "$pid" 2>/dev/null ;;
        esac
        # The services rewritten in components/node-services run the same node
        # from the rootfs directory; their script path gives them away. Read with
        # mapfile, a builtin, so the sweep stays free of forks.
        if [ "$exe" = /usr/palm/nodejs/node ] || [ "$exe" = "$ROOTFS/usr/palm/nodejs/node" ]; then
            { mapfile -d '' -t args < "$d/cmdline"; } 2>/dev/null || args=()
            case " ${args[*]} " in
                *" /usr/palm/node-services/"*) kill "$pid" 2>/dev/null ;;
            esac
        fi
    done

    pkill -x ls-hubd 2>/dev/null
    rm -f /tmp/pipcserver.sysmgr

    # A moment to go quietly, then insist. Without this a service that ignores
    # TERM outlives the supervisor, which is the whole problem this file exists
    # to fix.
    sleep 2
    pkill -9 -x LunaSysMgr 2>/dev/null
    pkill -9 -x WebAppMgr  2>/dev/null
    pkill -9 -x ls-hubd    2>/dev/null
}

if [ "${1:-}" = "--down" ]; then
    teardown
    exit 0
fi

# One session at a time, and a second start leaves the first alone.
#
# Found live, with the installed AppImage: webOS was opened from the menu while
# a session was already up, and then once more half a minute later. Each start
# begins with the teardown below, which finds processes by name, so the second
# supervisor killed the running session and the two then kept tearing down each
# other's stack. It settled on LunaSysMgr and WebAppMgr alive with no ls-hubd --
# a window on screen with no bus behind it.
#
# The lock is taken before that teardown and before the trap, so a refused start
# changes nothing. Its path is fixed rather than under $LOGDIR: what two sessions
# fight over is fixed too -- /tmp/pipcserver.sysmgr and the hub's sockets -- and
# the AppImage, the .deb and a development tree all share them.
# WEBOS_SESSION_LOCK exists for tests/session-lock.
#
# flock is released when the descriptor closes, so a crashed supervisor leaves
# nothing stale behind. That only holds if nothing else keeps the descriptor
# open, which is why every stage below is started with 9>&-: a service that
# outlived the supervisor would otherwise hold the lock and refuse every later
# start.
LOCK="${WEBOS_SESSION_LOCK:-/tmp/webos-ce-session.lock}"
if ! exec 9>"$LOCK"; then
    say "webos: cannot open the session lock $LOCK"
    exit 1
fi
if ! flock -n 9; then
    say "webos: webOS CE is already running; not starting a second session"
    say "       (to stop the running one: webos-session.sh --down)"
    exit 1
fi

while :; do

# Start from a clean slate rather than fighting whatever a previous session
# left: a held IPC socket or a stale hub is the usual reason a start looks like
# it worked and then nothing answers. On a restart, that previous session is the
# one that just asked to go.
teardown >/dev/null 2>&1
trap teardown EXIT INT TERM

mkdir -p "$LOGDIR"
rm -f "$WEBOS_SESSION_REQUEST"
say "webos: starting the bus"
"$LAUNCH" bus      > "$LOGDIR/bus.log"      2>&1 9>&-
sleep 3
say "webos: starting the services"
"$LAUNCH" services > "$LOGDIR/services.log" 2>&1 9>&-
sleep 3

if [ ! -e "$SENTINEL" ]; then
    say "webos: first start for this tree, loading the database"
    if "$LAUNCH" init > "$LOGDIR/init.log" 2>&1 9>&-; then
        : > "$SENTINEL" 2>/dev/null || true
    else
        # Not fatal on its own, and worth saying out loud rather than leaving
        # someone to discover it as apps that open empty: without the kinds,
        # db8 answers "kind not registered" to everything.
        say "webos: init failed -- apps may come up empty. See $LOGDIR/init.log"
    fi
else
    # tempdb does not survive a reboot of the host; see "tempdb" in the launcher.
    "$LAUNCH" tempdb > "$LOGDIR/tempdb.log" 2>&1 9>&- \
        || say "webos: registering tempdb failed -- see $LOGDIR/tempdb.log"
fi

say "webos: starting the shell"
"$LAUNCH" run      > "$LOGDIR/run.log"      2>&1 9>&-

# run returns when LunaSysMgr exits. The trap does the rest, which is the
# difference between this and running the launcher by hand -- unless webOS's own
# Restart is what ended it, in which case the loop brings everything back.
say "webos: the shell exited"
if [ "$(cat "$WEBOS_SESSION_REQUEST" 2>/dev/null)" = restart ]; then
    say "webos: restart requested, bringing the session back"
    continue
fi
break

done
