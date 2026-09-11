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

# Quoted heredoc: CMake's own ${} must survive, so the paths go in afterwards.
cat > "$BUILD/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.16)
project(node-shim-test CXX)
add_subdirectory(@ROOT@/components/node-v8-shim shim)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)

webos_node_addon(persistent @ROOT@/tests/node-shim/persistent_addon.cpp)

webos_node_addon(pmloglib @ROOT@/components/nodejs-module-webos-pmlog/src/pmloglib.cpp)
target_include_directories(pmloglib PRIVATE @BUILD@)

# The one that matters: 2660 lines of HP's, none of them touched.
set(SYSBUS @ROOT@/components/nodejs-module-webos-sysbus/src)
webos_node_addon(palmbus
    ${SYSBUS}/node_ls2.cpp ${SYSBUS}/node_ls2_base.cpp ${SYSBUS}/node_ls2_call.cpp
    ${SYSBUS}/node_ls2_handle.cpp ${SYSBUS}/node_ls2_message.cpp
    ${SYSBUS}/node_ls2_utils.cpp ${SYSBUS}/node_ls2_error_wrapper.cpp)
target_include_directories(palmbus PRIVATE ${SYSBUS} ${GLIB_INCLUDE_DIRS}
    @ROOT@/build-modern/staging/include)
target_link_directories(palmbus PRIVATE @ROOT@/build-modern/staging/lib)
target_link_libraries(palmbus PRIVATE ${GLIB_LIBRARIES} luna-service2)

# dynaload: loads a script into what V8 called its own context.
set(DYNA @ROOT@/components/nodejs-module-webos-dynaload/src)
webos_node_addon(webos ${DYNA}/node_webos.cpp ${DYNA}/external_string.cpp)
target_include_directories(webos PRIVATE ${DYNA})
target_link_libraries(webos PRIVATE boost_filesystem)
CMAKE
sed -i "s|@ROOT@|$ROOT|g; s|@BUILD@|$BUILD|g" "$BUILD/CMakeLists.txt"

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

cat > "$BUILD/build/check-palmbus.js" <<'JS'
const pb = require('./palmbus.node');
let bad = 0;
function has(cls, names) {
    const proto = pb[cls] && pb[cls].prototype;
    for (const n of names) {
        const ok = proto && typeof proto[n] === 'function';
        console.log(`  ${(cls + '.' + n).padEnd(40)} ${ok ? 'ok' : 'MISSING'}`);
        if (!ok) bad++;
    }
}
console.log(`  exports: ${Object.keys(pb).sort().join(', ')}`);
// HP's own methods, off the FunctionTemplate prototypes.
has('Handle', ['call', 'watch', 'subscribe', 'registerMethod', 'cancel', 'pushRole', 'unregister']);
has('Message', ['payload', 'respond', 'category', 'method', 'token']);
// And the EventEmitter the shim splices in, which method_dispatcher.js needs
// for addListener('request').
has('Handle', ['addListener', 'on', 'emit', 'removeListener']);
has('Call', ['addListener', 'on', 'emit']);
console.log(bad === 0 ? 'OK' : 'FAIL');
process.exit(bad === 0 ? 0 : 1);
JS

cd "$BUILD/build" || exit 1
export LD_LIBRARY_PATH="$ROOT/build-modern/staging/lib:${LD_LIBRARY_PATH:-}"

cat > "$BUILD/build/check-persistent.js" <<'JS'
const m = require('./persistent.node');
let bad = 0;
function check(what, got, want) {
    const ok = got === want;
    console.log(`  ${what.padEnd(40)} ${JSON.stringify(got)}${ok ? '' : '  <- expected ' + JSON.stringify(want)}`);
    if (!ok) bad++;
}
// Both are read long after the init scope that created them closed.
check('Persistent<String> survives', m.readSymbol(), 'response');
check('typeof it is still string', typeof m.readSymbol(), 'string');
check('Persistent<Object> survives', m.readObjectKey(), 'still here');
console.log(bad === 0 ? 'OK' : 'FAIL');
process.exit(bad === 0 ? 0 : 1);
JS

echo "Persistent across scopes:"
node check-persistent.js || exit 1

echo
echo "pmloglib -- the whole path, native and embedded JavaScript:"
node check.js || exit 1

echo
echo "palmbus -- 2660 lines of HP's, unmodified:"
node check-palmbus.js || exit 1

# HP's own test for dynaload, unchanged apart from where it looks for the addon.
# It loads a second script and that script has to see __filename and __dirname,
# which only works if the context emulation gives it the names the real one did.
sed "s|require('webos')|require('$BUILD/build/webos.node')|" \
    "$ROOT/components/nodejs-module-webos-dynaload/src/test/test_include.js" \
    > "$BUILD/build/test_include.js"
cp "$ROOT/components/nodejs-module-webos-dynaload/src/test/another_script.js" "$BUILD/build/"

echo
echo "dynaload -- HP's own include test:"
out="$(node test_include.js 2>&1)"
echo "$out" | sed 's/^/  /'
if echo "$out" | grep -q "__filename = .*/another_script.js" \
   && echo "$out" | grep -q "__dirname = /"; then
    echo "OK"
else
    echo "FAIL: the included script did not get its own __filename and __dirname"
    exit 1
fi
