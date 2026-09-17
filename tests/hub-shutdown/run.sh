#!/bin/bash
# ls-hubd leaves when it is told to, even while it is busy.
#
# Its SIGTERM handler used to call g_main_loop_quit() from inside the signal.
# That is not async-signal-safe: a signal landing while the main loop held its
# context lock waited on that lock forever. Found in CI, where a node-services
# test file never finished because its hub was asleep on a futex, inside
# _HandleShutdown (#48).
#
# The race needs the hub to be working when the signal arrives, so each round
# starts a hub, keeps clients calling it, and sends SIGTERM after a short,
# varying delay, as a burst. A hub still alive a few seconds later is a hang.
#
# Runs in a mount and pid namespace of its own: the hub's sockets have fixed
# paths under /tmp, and a running session must not see these hubs.
#
#   tests/hub-shutdown/run.sh [rounds]
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
STAGING="$ROOT/build/staging"
ROUNDS="${1:-60}"

[ -x "$STAGING/usr/sbin/ls-hubd" ] || { echo "SKIP: ls-hubd is not staged"; exit 77; }
[ -x "$STAGING/usr/bin/luna-send" ] || { echo "SKIP: luna-send is not staged"; exit 77; }

if [ -z "${HUB_SHUTDOWN_INSIDE:-}" ]; then
    if [ "$(id -u)" = 0 ] && [ -f /.dockerenv ]; then
        # CI: the container's /tmp is its own, and nothing else is on a bus.
        exec env HUB_SHUTDOWN_INSIDE=1 "$0" "$@"
    fi
    command -v bwrap >/dev/null || { echo "SKIP: no bwrap"; exit 77; }
    exec bwrap --dev-bind / / --tmpfs /tmp --unshare-pid --proc /proc --die-with-parent \
        env HUB_SHUTDOWN_INSIDE=1 "$0" "$@"
fi

export LD_LIBRARY_PATH="$STAGING/usr/lib:$STAGING/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
T="$(mktemp -d)"
mkdir -p "$T/services" "$T/roles"
cat > "$T/hub.conf" <<CONF
[General]
LocalSocketDirectory=/tmp
PidDirectory=$T
EnableStaticServices=false
[Dynamic Services]
Directories=$T/services
[Security]
Enabled=false
Directories=$T/roles
CONF

hangs=0
for round in $(seq 1 "$ROUNDS"); do
    rm -f /tmp/com.palm.private_hub
    "$STAGING/usr/sbin/ls-hubd" --conf "$T/hub.conf" > /dev/null 2>&1 &
    hub=$!
    for _ in $(seq 1 200); do
        [ -S /tmp/com.palm.private_hub ] && break
        sleep 0.01
    done

    # Clients that keep the hub's main loop busy. A call the hub never answers
    # because it left hangs luna-send, hence the timeout.
    for _ in 1 2 3 4 5 6 7 8; do
        ( while kill -0 "$hub" 2>/dev/null; do
              timeout -s KILL 2 "$STAGING/usr/bin/luna-send" -n 1 luna://com.webosce.nobody/x '{}' > /dev/null 2>&1
          done ) > /dev/null 2>&1 &
    done

    sleep "0.0$((round % 9 + 1))"
    # A burst rather than one signal: the handler can land anywhere in the
    # loop, including in the teardown that follows the first quit, which
    # widens the window a single signal only rarely hits.
    for _ in $(seq 1 50); do
        kill -TERM "$hub" 2>/dev/null || break
    done

    for _ in $(seq 1 300); do
        kill -0 "$hub" 2>/dev/null || break
        sleep 0.01
    done
    if kill -0 "$hub" 2>/dev/null; then
        hangs=$((hangs + 1))
        echo "round $round: the hub did not leave after SIGTERM"
        kill -KILL "$hub" 2>/dev/null
    fi
    wait 2>/dev/null
done

echo "$hangs of $ROUNDS hubs hung on SIGTERM"
[ "$hangs" = 0 ]
