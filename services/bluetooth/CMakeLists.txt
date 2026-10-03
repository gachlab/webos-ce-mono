# bluetooth: com.palm.btmonitor and com.palm.bluetooth answered from BlueZ, for
# a machine whose Bluetooth is a laptop's adapter on BlueZ 5 rather than a
# phone's. Ours, not HP's: HP's services for these names shipped only on the
# device and were never released as source, so nothing of theirs is here -- the
# specification is the one surviving consumer, the system menu drawer in
# reference/luna-sysmgr-ce.
#
# Built by tools/build.sh's "bluetooth" stage against the staging tree, the same
# shape as services/nm-connectionmanager and services/sysfs-powerd.
cmake_minimum_required(VERSION 3.16)
project(bluetooth CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
# GDBus, which is how BlueZ is read and driven. No extra dependency: gio ships
# with glib, which the service already links for its main loop. It is the reason
# this is C++ and not JavaScript, exactly as nm-connectionmanager's README says.
pkg_check_modules(GIO REQUIRED gio-2.0)
pkg_check_modules(LS2 REQUIRED luna-service2)
# cjson is named explicitly for the same reason as nm-connectionmanager: staging
# keeps libcjson in lib/ while luna-service2 sits in usr/lib/ with an rpath of
# $ORIGIN:$ORIGIN/.., so the json_* symbols the request parsers use would go
# unresolved at link time otherwise.
pkg_check_modules(CJSON REQUIRED cjson)

add_executable(bluetooth src/main.cpp src/bluez_client.cpp src/bluez_agent.cpp)
target_include_directories(bluetooth PRIVATE
    src ${GLIB_INCLUDE_DIRS} ${GIO_INCLUDE_DIRS} ${LS2_INCLUDE_DIRS} ${CJSON_INCLUDE_DIRS})
target_link_directories(bluetooth PRIVATE
    ${LS2_LIBRARY_DIRS} ${CJSON_LIBRARY_DIRS})
target_link_libraries(bluetooth PRIVATE
    ${LS2_LIBRARIES} ${CJSON_LIBRARIES} ${GIO_LIBRARIES} ${GLIB_LIBRARIES})
# -Wextra minus the one warning HP's own LSMethod tables raise: every method
# array in the tree is written { "name", function } and leaves flags to its zero
# default, and these follow that -- the same exception nm-connectionmanager makes.
target_compile_options(bluetooth PRIVATE
    -Wall -Wextra -Wno-missing-field-initializers)

# usr/sbin, where HP's system services sit in staging; assemble-rootfs.sh finds
# the binary there from the Exec= of the .service file.
install(TARGETS bluetooth RUNTIME DESTINATION usr/sbin)
