# Sourced, not run. Finds the node pinned in tools/node-version and puts it first
# on PATH, so everything below uses the node that ships rather than whatever the
# host happens to have.
#
#   . tools/node-home.sh && echo "$NODE_HOME"
#
# Looks in $WEBOS_NODE_HOME, then build/node-dist/current. Returns non-zero, and
# leaves PATH alone, when neither holds the pinned version: the caller decides
# whether that is fatal.

_webos_node_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=node-version
. "$_webos_node_root/tools/node-version"

for _candidate in "${WEBOS_NODE_HOME:-}" "$_webos_node_root/build/node-dist/current"; do
    [ -n "$_candidate" ] || continue
    if [ -x "$_candidate/bin/node" ] \
       && [ "$("$_candidate/bin/node" -v 2>/dev/null)" = "$NODE_VERSION" ]; then
        NODE_HOME="$(cd "$_candidate" && pwd -P)"
        export NODE_HOME
        export PATH="$NODE_HOME/bin:$PATH"
        unset _candidate _webos_node_root
        return 0
    fi
done
unset _candidate _webos_node_root
return 1
