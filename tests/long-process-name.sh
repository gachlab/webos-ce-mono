#!/bin/bash
# Process detection has to work for names longer than 15 characters.
#
# Linux truncates a task's comm to 15 characters (TASK_COMM_LEN is 16 including
# the terminator). `pgrep -x` and `pkill -x` match against comm, so for a longer
# name they match nothing and say so only on stderr:
#
#   pgrep: pattern that searches for process name longer than 15 characters
#          will result in zero matches
#
# LunaUniversalSearchMgr is 22. For a whole session it was reported dead in every
# status line while thirteen copies of it were running -- the same truncation
# stopped pkill from reaping them, so one leaked per launch, and the conclusion
# written down was that the service died inside the bwrap namespace.
#
# This runs run-lunasysmgr.sh's helpers against a process whose name is too long
# and checks they find it and can stop it.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SCRIPT="$HERE/../tools/run-lunasysmgr.sh"

# Pull in the helpers without running the script: everything below the case
# statement needs an environment this test does not have.
eval "$(sed -n '/^service_pids()/,/^}/p;/^service_running()/,/^}/p;/^service_stop()/,/^}/p' "$SCRIPT")"

if ! declare -F service_pids >/dev/null; then
    echo "FAIL: could not read the helpers out of $SCRIPT"
    exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

NAME=ThisNameIsWayTooLongForComm   # 27 characters
BIN="$TMP/$NAME"
cp "$(command -v sleep)" "$BIN"

"$BIN" 60 &
PID=$!
sleep 0.3

fail=0

# The premise: confirm the kernel really did truncate, so this test still means
# something if TASK_COMM_LEN ever changes.
COMM="$(cat /proc/$PID/comm 2>/dev/null)"
echo "name: $NAME (${#NAME} chars)"
echo "comm: $COMM (${#COMM} chars)"
if [ "$COMM" = "$NAME" ]; then
    echo "SKIP: this kernel does not truncate comm, nothing to test"
    kill $PID 2>/dev/null
    exit 0
fi

# What the old code did, kept as the contrast that motivates the helpers.
if pgrep -x "$NAME" >/dev/null 2>&1; then
    echo "unexpected: pgrep -x found it, so the bug this guards is gone"
    fail=1
else
    echo "pgrep -x          : finds nothing, as expected"
fi

if service_running "$BIN"; then
    echo "service_running   : finds it"
else
    echo "service_running   : MISSES IT"
    fail=1
fi

service_stop "$BIN"
sleep 0.3
if service_running "$BIN"; then
    echo "service_stop      : DID NOT STOP IT"
    fail=1
else
    echo "service_stop      : stopped it"
fi

kill $PID 2>/dev/null
[ $fail -eq 0 ] && echo OK || echo FAIL
exit $fail
