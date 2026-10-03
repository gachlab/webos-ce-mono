# zeroconf-avahi: com.palm.zeroconf answered from the host's Avahi
# (org.freedesktop.Avahi) instead of from HP's device-only service. Ours, not
# HP's: nothing in the CE drop provides com.palm.zeroconf, and HP never released
# it. Unlike the other services here the API is not an HP contract -- no caller
# fixes it -- so it is the DNS-SD vocabulary, marked provisional in the source.
#
# Built by tools/build.sh's "zeroconf" stage against the staging tree, so
# PKG_CONFIG_PATH finds luna-service2 there. Same shape as
# services/nm-connectionmanager and services/bluetooth, the other services that
# reach a host daemon over GDBus.
cmake_minimum_required(VERSION 3.16)
project(zeroconf-avahi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
# GDBus, which is how Avahi is reached. No extra dependency: gio ships with
# glib, which every service here already links for its main loop. It is the
# reason this is C++ and not JavaScript, and the reason there is no avahi-client
# library here -- gio's own D-Bus client is enough.
pkg_check_modules(GIO REQUIRED gio-2.0)
pkg_check_modules(LS2 REQUIRED luna-service2)
# Named explicitly because luna-service2 needs it and cannot find it by itself
# here: staging puts libcjson in lib/ while luna-service2 sits in usr/lib/ with
# an rpath of $ORIGIN:$ORIGIN/.., so every json_* symbol went unresolved at link
# time. sysfs-powerd and nm-connectionmanager declare cjson for the same reason.
pkg_check_modules(CJSON REQUIRED cjson)

add_executable(zeroconf-avahi src/main.cpp src/avahi_client.cpp)
target_include_directories(zeroconf-avahi PRIVATE
    src ${GLIB_INCLUDE_DIRS} ${GIO_INCLUDE_DIRS} ${LS2_INCLUDE_DIRS} ${CJSON_INCLUDE_DIRS})
target_link_directories(zeroconf-avahi PRIVATE
    ${LS2_LIBRARY_DIRS} ${CJSON_LIBRARY_DIRS})
target_link_libraries(zeroconf-avahi PRIVATE
    ${LS2_LIBRARIES} ${CJSON_LIBRARIES} ${GIO_LIBRARIES} ${GLIB_LIBRARIES})
target_compile_options(zeroconf-avahi PRIVATE
    -Wall -Wextra -Wno-missing-field-initializers)

install(TARGETS zeroconf-avahi RUNTIME DESTINATION usr/sbin)
