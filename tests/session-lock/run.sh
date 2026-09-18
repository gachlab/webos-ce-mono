#!/bin/bash
# tools/webos-session.sh runs one session at a time, and a second start leaves
# the first alone.
#
# Found live with the installed AppImage: opening webOS while a session was up
# made the new supervisor's startup teardown kill the running one, and two
# starts settled on a shell with no bus behind it.
#
# Nothing real is touched. The supervisor runs against a stub launcher, and its
# teardown's pkill, rm and sleep are stubs on PATH that only record their
# arguments -- so a start that should have been refused, but tore down anyway,
# shows up as a non-empty record instead of as a killed session.
#
# One thing cannot be stubbed: teardown kills JavaScript services with bash's
# builtin kill. The cases that let a start go through would therefore reach a
# real session's services, so they are skipped while one is running. The case
# that matters most -- refusing -- never reaches teardown and always runs.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../.."
SESSION="$ROOT/tools/webos-session.sh"
FLOCK=/usr/bin/flock
SLEEP=/bin/sleep
RM=/bin/rm

command -v "$FLOCK" >/dev/null || { echo "SKIP: no flock"; exit 0; }

T="$(mktemp -d)"
trap '"$RM" -rf "$T"' EXIT
mkdir -p "$T/stub" "$T/log" "$T/rootfs/var/luna"

failures=0
check() {
    if [ "$1" = ok ]; then printf "  %-66s OK\n" "$2"
    else printf "  %-66s <-- FAIL\n" "$2"; failures=$((failures + 1)); fi
}

for tool in pkill rm sleep; do
    cat > "$T/stub/$tool" <<STUB
#!/bin/sh
echo "$tool \$*" >> "$T/teardown.calls"
STUB
    chmod +x "$T/stub/$tool"
done

# The launcher: records every stage. "bus" leaves a long-lived child behind, the
# way ls-hubd outlives the command that started it.
cat > "$T/launch" <<STUB
#!/bin/sh
echo "\$1" >> "$T/launch.calls"
if [ "\$1" = bus ]; then
    $SLEEP 30 &
    echo \$! > "$T/daemon.pid"
fi
exit 0
STUB
chmod +x "$T/launch"

run_session() {
    PATH="$T/stub:$PATH" \
    WEBOS_LAUNCHER="$T/launch" WEBOS_ROOTFS="$T/rootfs" WEBOS_LOGDIR="$T/log" \
    WEBOS_SESSION_LOCK="$T/lock" \
        timeout 20 "$SESSION" 2> "$T/stderr"
}

echo "a start while another session holds the lock"
# One process holding it, so killing that process frees it. `flock FILE sleep`
# would not do: the sleep inherits the descriptor and outlives a killed flock --
# the very leak the supervisor's 9>&- exists to prevent.
( exec 8>"$T/lock"; "$FLOCK" 8; exec "$SLEEP" 30 ) &
holder=$!
for _ in 1 2 3 4 5 6 7 8 9 10; do
    "$FLOCK" -n "$T/lock" true 2>/dev/null || break
    "$SLEEP" 0.1
done
run_session; rc=$?
[ "$rc" -ne 0 ] && check ok "is refused (exit $rc)" || check fail "is refused (exit $rc)"
grep -q "already running" "$T/stderr" && check ok "and says why" || check fail "and says why"
[ ! -s "$T/teardown.calls" ] && check ok "without running teardown -- the live session is left alone" \
    || check fail "without running teardown ($(tr '\n' ';' < "$T/teardown.calls"))"
[ ! -s "$T/launch.calls" ] && check ok "and without starting any stage" || check fail "and without starting any stage"
kill -0 "$holder" 2>/dev/null && check ok "the running session still holds its lock" || check fail "the running session still holds its lock"
kill "$holder" 2>/dev/null; wait "$holder" 2>/dev/null

real=0
for d in /proc/[0-9]*; do
    case "$(readlink "$d/cwd" 2>/dev/null)" in */usr/palm/services/*) real=1; break ;; esac
    case "$(readlink "$d/exe" 2>/dev/null)" in /usr/palm/nodejs/node) real=1; break ;; esac
done
if [ "$real" = 1 ]; then
    echo
    echo "SKIP the start-through cases: a real session is running, and teardown's builtin kill would reach its services"
else
    echo
    echo "a start with nothing else running"
    "$RM" -f "$T/teardown.calls" "$T/launch.calls"
    # A service from services/node-services, as teardown sees one: the
    # rootfs's node running a script under /usr/palm/node-services. bash stands
    # in for node, waiting on a fifo so it stays itself (a sleep would be a
    # different executable).
    mkdir -p "$T/rootfs/usr/palm/nodejs"
    cp /bin/bash "$T/rootfs/usr/palm/nodejs/node"
    mkfifo "$T/wait"
    "$T/rootfs/usr/palm/nodejs/node" -c 'read -t 30 <> "$1"' /usr/palm/node-services/services/fake/main.ts "$T/wait" &
    fake=$!
    run_session; rc=$?
    [ "$rc" -eq 0 ] && check ok "goes through (exit 0)" || check fail "goes through (exit $rc; $(head -3 "$T/stderr" | tr '\n' ' '))"
    [ "$(tr '\n' ' ' < "$T/launch.calls" 2>/dev/null)" = "bus services init run " ] \
        && check ok "and starts every stage in order" \
        || check fail "and starts every stage in order (got: $(tr '\n' ' ' < "$T/launch.calls" 2>/dev/null))"
    "$SLEEP" 0.2
    kill -0 "$fake" 2>/dev/null \
        && { check fail "and stops the rewritten services too"; kill "$fake"; } \
        || check ok "and stops the rewritten services too"
    wait "$fake" 2>/dev/null

    echo
    echo "a later start of the same tree"
    daemon=$(cat "$T/daemon.pid" 2>/dev/null)
    [ -n "$daemon" ] && kill "$daemon" 2>/dev/null
    "$RM" -f "$T/teardown.calls" "$T/launch.calls"
    run_session; rc=$?
    [ "$(tr '\n' ' ' < "$T/launch.calls" 2>/dev/null)" = "bus services tempdb run " ] \
        && check ok "registers tempdb again instead of the whole init" \
        || check fail "registers tempdb again instead of the whole init (got: $(tr '\n' ' ' < "$T/launch.calls" 2>/dev/null))"

    echo
    echo "after that session has ended"
    daemon=$(cat "$T/daemon.pid" 2>/dev/null)
    [ -n "$daemon" ] && kill -0 "$daemon" 2>/dev/null \
        && check ok "a child it started is still running (as ls-hubd would be)" \
        || check fail "a child it started is still running"
    "$FLOCK" -n "$T/lock" true \
        && check ok "the lock is free -- that child did not inherit it" \
        || check fail "the lock is free -- a surviving child still holds it, and every later start would be refused"
    [ -n "$daemon" ] && kill "$daemon" 2>/dev/null
fi

echo
[ "$failures" -eq 0 ] && { echo "OK"; exit 0; } || { echo "FAILED: $failures"; exit 1; }
