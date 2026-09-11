#!/bin/bash
# A webOS JavaScript service must boot on a modern node and answer on the bus.
#
# This is the whole stack above the addons: run-js-service, bootstrap-node.js,
# mojoloader, the foundations and mojoservice frameworks, and a service's own
# assistant -- all of it HP's, none of it changed. It needs the bus up, so it is
# a check you run rather than one ctest runs.
#
#   tests/node-shim/service.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../.."
ROOTFS="$ROOT/build-modern/rootfs"
STAGING="$ROOT/build-modern/staging"

command -v bwrap >/dev/null || { echo "SKIP: no bwrap"; exit 0; }
NODE_REAL="$(command -v node 2>/dev/null)"
[ -n "$NODE_REAL" ] || { echo "SKIP: no node"; exit 0; }
[ -x "$STAGING/usr/bin/luna-send" ] || { echo "SKIP: luna-send not staged"; exit 0; }
[ -f "$ROOTFS/usr/palm/nodejs/palmbus.node" ] || {
    echo "SKIP: the addons are not installed; run tests/node-shim/run.sh and copy them"
    exit 0; }

# The service asks com.palm.activitymanager to track its work before it answers,
# so the static services have to be up. Restarting the bus kills them.
if ! LD_LIBRARY_PATH="$STAGING/lib:$STAGING/usr/lib" timeout 6 \
     "$STAGING/usr/bin/luna-send" -n 1 palm://com.palm.activitymanager/ping '{}' 2>&1 \
     | grep -qv "does not exist"; then
    echo "SKIP: com.palm.activitymanager is not up -- run tools/run-lunasysmgr.sh servicios"
    exit 0
fi

INNER="$(mktemp)"
trap 'rm -f "$INNER"' EXIT
cat > "$INNER" <<INNEREOF
#!/bin/bash
export LD_LIBRARY_PATH="$STAGING/lib:$STAGING/usr/lib"
export NODE_OPTIONS="--require /usr/palm/nodejs/webos-node-compat.js"
/usr/lib/luna/run-js-service -n /usr/palm/services/com.palm.location >/tmp/webos-service-test.log 2>&1 &
# The service quits itself after a few idle seconds, so ask before it does.
sleep 3
"$STAGING/usr/bin/luna-send" -n 1 palm://com.palm.location/getCurrentPosition '{}' 2>&1 | head -1
INNEREOF
chmod +x "$INNER"

rebind=()
for d in /usr/*;     do [ -e "$d" ] && rebind+=(--bind "$d" "$d"); done
rebind+=(--tmpfs /usr/lib)
for d in /usr/lib/*; do [ -e "$d" ] && rebind+=(--bind "$d" "$d"); done

out="$(timeout 40 bwrap --dev-bind / / \
    --bind "$ROOTFS/etc/palm" /etc/palm \
    --tmpfs /usr "${rebind[@]}" \
    --bind "$ROOTFS/usr/palm" /usr/palm \
    --bind "$ROOTFS/usr/lib/luna" /usr/lib/luna \
    --bind "$NODE_REAL" /usr/palm/nodejs/node \
    "$INNER" 2>&1 | grep -vE '^\[ev\]|^\[shim\]')"

echo "$out" | sed 's/^/  /'
if echo "$out" | grep -q '"returnValue":true'; then
    echo "OK: the service booted and answered on the bus"
    exit 0
fi
echo "FAIL: no answer. /tmp/webos-service-test.log has the service's own output."
exit 1
