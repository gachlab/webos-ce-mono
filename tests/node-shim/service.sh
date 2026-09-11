#!/bin/bash
# webOS's JavaScript services must start on demand and answer on the bus.
#
# Nothing here starts a service. Each one is simply called, the way anything on
# the device would call it, and ls-hubd has to notice it is not running, start it
# through its .service file, and route the call once it registers. On this build
# that goes through run-lunasysmgr.sh js-service, which puts the service inside
# the namespace the hub itself stays out of.
#
# So one call exercises the whole stack: the hub's activation, the namespace,
# run-js-service and bootstrap-node.js, mojoloader and the frameworks, the
# service's own assistant, and components/node-v8-shim underneath all of it --
# with none of HP's JavaScript changed.
#
# Needs the bus and the static services up (tools/run-lunasysmgr.sh bus, then
# services), so it is run by hand rather than by ctest.
#
#   tests/node-shim/service.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../.."
STAGING="$ROOT/build/staging"
SEND="$STAGING/usr/bin/luna-send"
export LD_LIBRARY_PATH="$STAGING/lib:$STAGING/usr/lib"

[ -x "$SEND" ] || { echo "SKIP: luna-send is not staged"; exit 0; }

# The services ask com.palm.activitymanager to track their work before they
# answer, so the static services have to be up.
if timeout 6 "$SEND" -n 1 palm://com.palm.activitymanager/ping '{}' 2>&1 | grep -q "does not exist"; then
    echo "SKIP: com.palm.activitymanager is not up -- run tools/run-lunasysmgr.sh services"
    exit 0
fi

# One method per service. Several are called without the parameters they want:
# an error from the service itself still proves it started and answered, which
# is the question. Replies from the hub instead -- the service never came up, or
# the call never reached it -- are what count as a failure.
CALLS="
com.palm.location                   getCurrentPosition
com.palm.connectionmanager          getStatus
com.palm.service.accounts           listAccountTemplates
com.palm.service.contacts           countVCardContacts
com.palm.service.contacts.linker    manualUnlink
com.palm.service.calendar.reminders onInit
"

HUB_FAILURE='is not running|does not exist|not allowed|Permission denied|[Ii]nvalid permissions'
failed=0
OUT="$(mktemp)"; ERR="$(mktemp)"
trap 'rm -f "$OUT" "$ERR"' EXIT

# ask <- for the private bus, or -P for the public one> <service> <method>
#
# Mind luna-send's flags: -P means the PUBLIC bus and no flag means private. The
# opposite reading is natural and wrong, and an earlier round of these checks was
# labelled backwards because of it.
#
# luna-send is given /dev/null for stdin and files for its output, not a pipe.
# An earlier version read the services from a heredoc in a while loop and took
# the reply through $(... | head -1); every call came back empty in no time at
# all, while the hub's own log showed each service starting and handling its
# call. The loop was the thing being measured, not the services.
ask() {
    local flag="$1"
    if [ "$flag" = - ]; then
        timeout 45 "$SEND" -n 1 "palm://$2/$3" '{}' </dev/null >"$OUT" 2>"$ERR"
    else
        timeout 45 "$SEND" "$flag" -n 1 "palm://$2/$3" '{}' </dev/null >"$OUT" 2>"$ERR"
    fi
    cat "$OUT" "$ERR" | head -1
}

for entry in $(echo "$CALLS" | awk 'NF == 2 {print $1 "/" $2}'); do
    svc="${entry%%/*}"; method="${entry#*/}"
    # The private bus first -- every com.palm service listens there, which is
    # what HP's controller_service.js says and does. The public bus only if that
    # fails: a service registers there only when it has commands marked
    # "public", so a non-public method not answering on -P is by design.
    reply="$(ask - "$svc" "$method")"
    if [ -z "$reply" ] || echo "$reply" | grep -qE "$HUB_FAILURE"; then
        reply="$(ask -P "$svc" "$method")"
    fi
    if [ -z "$reply" ]; then
        verdict="NO REPLY"
    elif echo "$reply" | grep -qE "$HUB_FAILURE"; then
        verdict="HUB REFUSED"
    else
        verdict="answered"
    fi
    printf "  %-36s %-12s %s\n" "$svc" "$verdict" "$(echo "$reply" | cut -c1-70)"
    [ "$verdict" = answered ] || failed=$((failed + 1))
done

if [ "$failed" -eq 0 ]; then
    echo "OK: every JavaScript service was started on demand and answered"
    exit 0
fi
echo "FAIL: $failed service(s) did not answer. ls-hubd's log is /tmp/webos/ls-priv.log;"
echo "      a service started by the hub writes to it too."
exit 1
