# keymanager-secret: com.palm.keymanager answered from the host's Secret
# Service (org.freedesktop.secrets) instead of from a native encrypted store a
# phone's lock-screen passcode unlocked. Ours, not HP's: nothing in the CE drop
# provides com.palm.keymanager, so LunaSysMgr's Security.cpp and the accounts
# service's KeyStore have been calling a service that is not there.
#
# Built by tools/build.sh's "keymanager" stage against the staging tree, so
# PKG_CONFIG_PATH finds luna-service2 there. Same shape as
# services/nm-connectionmanager, which is the other service here that reaches a
# host daemon over GDBus.
cmake_minimum_required(VERSION 3.16)
project(keymanager-secret CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
# GDBus, which is how the Secret Service is reached. No extra dependency: gio
# ships with glib, which every service here already links for its main loop. It
# is the reason this is C++ and not JavaScript, and the reason there is no
# libsecret here -- gio's own D-Bus client is enough, and libsecret is not on
# the build host anyway.
pkg_check_modules(GIO REQUIRED gio-2.0)
pkg_check_modules(LS2 REQUIRED luna-service2)
# Named explicitly because luna-service2 needs it and cannot find it by itself
# here: staging puts libcjson in lib/ while luna-service2 sits in usr/lib/ with
# an rpath of $ORIGIN:$ORIGIN/.., so every json_* symbol went unresolved at link
# time. sysfs-powerd and nm-connectionmanager declare cjson for the same reason.
pkg_check_modules(CJSON REQUIRED cjson)

add_executable(keymanager-secret src/main.cpp src/secret_client.cpp)
target_include_directories(keymanager-secret PRIVATE
    src ${GLIB_INCLUDE_DIRS} ${GIO_INCLUDE_DIRS} ${LS2_INCLUDE_DIRS} ${CJSON_INCLUDE_DIRS})
target_link_directories(keymanager-secret PRIVATE
    ${LS2_LIBRARY_DIRS} ${CJSON_LIBRARY_DIRS})
target_link_libraries(keymanager-secret PRIVATE
    ${LS2_LIBRARIES} ${CJSON_LIBRARIES} ${GIO_LIBRARIES} ${GLIB_LIBRARIES})
# -Wextra, minus the one warning HP's own method tables would raise: every
# LSMethod array in the tree is written { "name", function } and leaves flags to
# its zero default, and these follow that.
target_compile_options(keymanager-secret PRIVATE
    -Wall -Wextra -Wno-missing-field-initializers)

# usr/sbin, where HP's system services sit in staging. assemble-rootfs.sh finds
# the binary there from the Exec= of the .service file and places it in
# /usr/lib/luna with the rest.
install(TARGETS keymanager-secret RUNTIME DESTINATION usr/sbin)
