# printmgr-cups: com.palm.printmgr from CUPS, ours rather than HP's. HP built
# printmgr on its own wprint library; nothing in the CE drop answers it, so the
# Print button in Email and the browser does nothing. Not a MANIFEST component --
# the MANIFEST stays an inventory of what HP released -- so it gets its own
# stage, built against staging like the other services here.
#
# libcups comes from the host, the way audiod-pipewire takes libpulse and
# sysfs-powerd takes glib: a .pc on the host, resolved through the stage's
# PKG_CONFIG_PATH.
cmake_minimum_required(VERSION 3.16)
project(printmgr-cups CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(LS2 REQUIRED luna-service2)
# Named explicitly for the same reason the other services name it: staging puts
# libcjson in lib/ while luna-service2 sits in usr/lib/ with an rpath of
# $ORIGIN:$ORIGIN/.., so every json_* symbol would otherwise go unresolved.
pkg_check_modules(CJSON REQUIRED cjson)
# The printing daemon's client library. From the host (libcups2-dev); the daemon
# it talks to is whatever cupsd the session can reach.
pkg_check_modules(CUPS REQUIRED cups)

add_executable(printmgr-cups src/main.cpp src/cups_client.cpp)
target_include_directories(printmgr-cups PRIVATE
    src ${GLIB_INCLUDE_DIRS} ${LS2_INCLUDE_DIRS} ${CJSON_INCLUDE_DIRS} ${CUPS_INCLUDE_DIRS})
target_link_directories(printmgr-cups PRIVATE
    ${LS2_LIBRARY_DIRS} ${CJSON_LIBRARY_DIRS} ${CUPS_LIBRARY_DIRS})
target_link_libraries(printmgr-cups PRIVATE
    ${LS2_LIBRARIES} ${CJSON_LIBRARIES} ${GLIB_LIBRARIES} ${CUPS_LIBRARIES})
# -Wextra, minus the one warning HP's own method tables raise: every LSMethod
# array in the tree is written { "name", function } and leaves flags to its zero
# default, and these follow that.
target_compile_options(printmgr-cups PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

# usr/sbin, where HP's system services sit in staging. assemble-rootfs.sh finds
# the binary there from the Exec= of the .service file and places it in
# /usr/lib/luna with the rest.
install(TARGETS printmgr-cups RUNTIME DESTINATION usr/sbin)
