# The three webOS node addons, built from HP's sources through the shim.
#
# HP's own CMakeLists for each of them are still in their components, untouched:
# they use webOS's cmake modules and pkg-config for a node that no longer exists.
# This is the modern build, and it lives with the shim that makes it possible
# rather than replacing what HP wrote.

cmake_minimum_required(VERSION 3.16)
project(webos-node-addons CXX)

add_subdirectory(.. shim)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)

set(COMPONENTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../components")
set(STAGING "${CMAKE_INSTALL_PREFIX}")

# --- pmloglib ---------------------------------------------------------------
# It embeds its JavaScript as a C array. HP generated that with "xxd -i", which
# lives in vim-common and is often not installed; python does the same job.
set(PMLOG "${COMPONENTS}/nodejs-module-webos-pmlog/src")
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/pmloglib.js.h"
    COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/embed-js.py"
            "${PMLOG}/pmloglib.js" "${CMAKE_CURRENT_BINARY_DIR}/pmloglib.js.h"
            pmloglib_js
    MAIN_DEPENDENCY "${PMLOG}/pmloglib.js"
    VERBATIM)
add_custom_target(pmloglib-js DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/pmloglib.js.h")

webos_node_addon(pmloglib "${PMLOG}/pmloglib.cpp")
add_dependencies(pmloglib pmloglib-js)
target_include_directories(pmloglib PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# --- palmbus (sysbus) -------------------------------------------------------
set(SYSBUS "${COMPONENTS}/nodejs-module-webos-sysbus/src")
webos_node_addon(palmbus
    "${SYSBUS}/node_ls2.cpp" "${SYSBUS}/node_ls2_base.cpp" "${SYSBUS}/node_ls2_call.cpp"
    "${SYSBUS}/node_ls2_handle.cpp" "${SYSBUS}/node_ls2_message.cpp"
    "${SYSBUS}/node_ls2_utils.cpp" "${SYSBUS}/node_ls2_error_wrapper.cpp")
target_include_directories(palmbus PRIVATE "${SYSBUS}" ${GLIB_INCLUDE_DIRS}
    "${STAGING}/include")
target_link_directories(palmbus PRIVATE "${STAGING}/lib")
target_link_libraries(palmbus PRIVATE ${GLIB_LIBRARIES} luna-service2)

# --- webos (dynaload) -------------------------------------------------------
set(DYNA "${COMPONENTS}/nodejs-module-webos-dynaload/src")
webos_node_addon(webos "${DYNA}/node_webos.cpp" "${DYNA}/external_string.cpp")
target_include_directories(webos PRIVATE "${DYNA}")
target_link_libraries(webos PRIVATE boost_filesystem)

# HP's run-js-service puts NODE_ADDONS on NODE_PATH and looks for them there.
install(TARGETS pmloglib palmbus webos LIBRARY DESTINATION usr/palm/nodejs)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../js/webos-node-compat.js"
        DESTINATION usr/palm/nodejs)
