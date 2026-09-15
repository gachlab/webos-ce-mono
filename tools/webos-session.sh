#!/bin/bash
# One process that owns the whole stack, for an installed webOS CE.
#
#   webos-session.sh            bring everything up and stay in the foreground
#   webos-session.sh --down     tear down whatever is running and exit
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

    for d in /proc/[0-9]*; do
        case "$(readlink "$d/cwd" 2>/dev/null)" in
            */usr/palm/services/*) kill "${d#/proc/}" 2>/dev/null ;;
        esac
    done

    for s in mojodb-luna LunaSysService sysfs-powerd filecache activitymanager LunaUniversalSearchMgr; do
        for d in /proc/[0-9]*; do
            [ "$(readlink "$d/exe" 2>/dev/null)" = "$ROOTFS/usr/lib/luna/$s" ] \
                && kill "${d#/proc/}" 2>/dev/null
        done
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
"$LAUNCH" bus      > "$LOGDIR/bus.log"      2>&1
sleep 3
say "webos: starting the services"
"$LAUNCH" services > "$LOGDIR/services.log" 2>&1
sleep 3

if [ ! -e "$SENTINEL" ]; then
    say "webos: first start for this tree, loading the database"
    if "$LAUNCH" init > "$LOGDIR/init.log" 2>&1; then
        : > "$SENTINEL" 2>/dev/null || true
    else
        # Not fatal on its own, and worth saying out loud rather than leaving
        # someone to discover it as apps that open empty: without the kinds,
        # db8 answers "kind not registered" to everything.
        say "webos: init failed -- apps may come up empty. See $LOGDIR/init.log"
    fi
fi

say "webos: starting the shell"
"$LAUNCH" run      > "$LOGDIR/run.log"      2>&1

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
