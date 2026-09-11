#!/bin/bash
# HP's node 0.4 addons must load and work in a modern node, unmodified.
#
# The point is not that pmloglib works -- it is 104 lines. The point is that its
# source was not touched: every V8 call in it goes through components/node-v8-shim,
# so the same shim carries sysbus (2660 lines) and dynaload without those being
# rewritten either.
#
# Checks the whole path: the native _logString, the constants, and the
# JavaScript the addon embeds and runs at load time to add info/warn/error/log.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../.."
BUILD="${1:-$ROOT/build-modern/node-shim-test}"

command -v node >/dev/null || { echo "SKIP: no node on PATH"; exit 0; }

rm -rf "$BUILD"; mkdir -p "$BUILD"

python3 - "$ROOT/components/nodejs-module-webos-pmlog/src/pmloglib.js" "$BUILD/pmloglib.js.h" <<'PY'
# HP's build used "xxd -i", which is in vim-common and often absent.
import sys
data = open(sys.argv[1], "rb").read()
rows = [", ".join("0x%02x" % b for b in data[i:i + 12]) for i in range(0, len(data), 12)]
open(sys.argv[2], "w").write(
    "unsigned char pmloglib_js[] = {\n  " + ",\n  ".join(rows) + "\n};\n"
    "unsigned int pmloglib_js_len = %d;\n" % len(data))
PY

cat > "$BUILD/CMakeLists.txt" <<CMAKE
cmake_minimum_required(VERSION 3.16)
project(node-shim-test CXX)
add_subdirectory($ROOT/components/node-v8-shim shim)
webos_node_addon(pmloglib $ROOT/components/nodejs-module-webos-pmlog/src/pmloglib.cpp)
target_include_directories(pmloglib PRIVATE $BUILD)
CMAKE

cmake -S "$BUILD" -B "$BUILD/build" >/dev/null 2>&1 || {
    echo "FAIL: cmake configure"; cmake -S "$BUILD" -B "$BUILD/build" 2>&1 | tail -5; exit 1; }
cmake --build "$BUILD/build" >/dev/null 2>&1 || {
    echo "FAIL: HP's source no longer compiles against the shim"
    cmake --build "$BUILD/build" 2>&1 | grep -E 'error:' | head -10; exit 1; }

cat > "$BUILD/build/check.js" <<'JS'
const m = require('./pmloglib.node');
let bad = 0;
function check(what, got, want) {
    const ok = JSON.stringify(got) === JSON.stringify(want);
    console.log(`  ${what.padEnd(40)} ${JSON.stringify(got)}${ok ? '' : '  <- expected ' + JSON.stringify(want)}`);
    if (!ok) bad++;
}
// Exported by the C++ half.
check('LOG_ERR', m.LOG_ERR, 3);
check('LOG_WARNING', m.LOG_WARNING, 4);
check('LOG_INFO', m.LOG_INFO, 6);
check('_logString is a function', typeof m._logString, 'function');
// Added by the JavaScript the addon embeds and runs from init().
for (const name of ['error', 'warn', 'info', 'log']) {
    check(`${name} is a function`, typeof m[name], 'function');
}
// And that decorator's own work: printf-style formatting, %j included.
m.name = 'shim-test';
check('formatting', m.info('a %s b %d c %j', 'x', 7, {k: 1}), 'a x b 7 c {"k":1}');
console.log(bad === 0 ? 'OK' : 'FAIL');
process.exit(bad === 0 ? 0 : 1);
JS

cd "$BUILD/build" && node check.js
