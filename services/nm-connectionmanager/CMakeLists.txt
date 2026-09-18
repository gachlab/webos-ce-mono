# nm-connectionmanager: com.palm.connectionmanager answered from NetworkManager,
# for a machine whose network is a laptop's wifi and ethernet rather than a
# phone's radio stack. Ours, not HP's: the CE drop ships
# components/pmnetconfigmanager-stub, which answers the same call with a
# constant that says "connected, over wifi, always".
#
# Built by tools/build.sh's "connmgr" stage against the staging tree, so
# PKG_CONFIG_PATH finds luna-service2 there. Same shape as
# services/sysfs-powerd, which is the other service in this tree that is ours.
cmake_minimum_required(VERSION 3.16)
project(nm-connectionmanager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
# GDBus, which is how NetworkManager is read. No extra dependency in practice:
# gio ships with glib, and every service here already links glib for its main
# loop. It is the reason this service is C++ and not JavaScript -- the
# alternative was bundling a D-Bus client library and the npm machinery to build
# it, for a service that needs neither.
pkg_check_modules(GIO REQUIRED gio-2.0)
pkg_check_modules(LS2 REQUIRED luna-service2)
# Named explicitly because luna-service2 needs it and cannot find it by itself
# here: staging puts libcjson in lib/ while luna-service2 sits in usr/lib/ with
# an rpath of $ORIGIN:$ORIGIN/.., so every json_* symbol went unresolved at link
# time. sysfs-powerd and LunaSysService declare cjson for the same reason.
pkg_check_modules(CJSON REQUIRED cjson)

add_executable(nm-connectionmanager src/main.cpp src/nm_client.cpp src/sleep_watch.cpp src/certificates.cpp)
# CJSON's include directory is named as well as its library: setWiredState parses
# its argument with it, and staging keeps that header in include/cjson while
# luna-service2's live in usr/include, so pkg-config is the only thing that knows
# where both are.
target_include_directories(nm-connectionmanager PRIVATE
    src ${GLIB_INCLUDE_DIRS} ${GIO_INCLUDE_DIRS} ${LS2_INCLUDE_DIRS} ${CJSON_INCLUDE_DIRS})
target_link_directories(nm-connectionmanager PRIVATE
    ${LS2_LIBRARY_DIRS} ${CJSON_LIBRARY_DIRS})
target_link_libraries(nm-connectionmanager PRIVATE
    ${LS2_LIBRARIES} ${CJSON_LIBRARIES} ${GIO_LIBRARIES} ${GLIB_LIBRARIES})
# -Wextra, minus the one warning HP's own method tables would raise: every
# LSMethod array in the tree is written { "name", function } and leaves flags to
# its zero default, and these follow that.
target_compile_options(nm-connectionmanager PRIVATE
    -Wall -Wextra -Wno-missing-field-initializers)

# usr/sbin, where HP's system services sit in staging. assemble-rootfs.sh finds
# the binary there from the Exec= of the .service file and places it in
# /usr/lib/luna with the rest.
install(TARGETS nm-connectionmanager RUNTIME DESTINATION usr/sbin)
