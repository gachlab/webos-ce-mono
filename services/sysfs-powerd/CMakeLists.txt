# sysfs-powerd: com.palm.power for a machine that has /sys/class/power_supply
# instead of a phone's fuel gauge. Ours, not HP's: nothing in the CE drop
# provides com.palm.power.
#
# Built by tools/build.sh's "powerd" stage against the staging tree, so
# PKG_CONFIG_PATH finds luna-service2 there.
cmake_minimum_required(VERSION 3.16)
project(sysfs-powerd CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(LS2 REQUIRED luna-service2)
# Named explicitly because luna-service2 needs it and cannot find it by itself
# here: staging puts libcjson in lib/ while luna-service2 sits in usr/lib/ with an
# rpath of $ORIGIN:$ORIGIN/.., so every json_* symbol went unresolved at link
# time. LunaSysService declares cjson for the same reason.
pkg_check_modules(CJSON REQUIRED cjson)

add_executable(sysfs-powerd src/main.cpp)
target_include_directories(sysfs-powerd PRIVATE src ${GLIB_INCLUDE_DIRS} ${LS2_INCLUDE_DIRS})
target_link_directories(sysfs-powerd PRIVATE ${LS2_LIBRARY_DIRS} ${CJSON_LIBRARY_DIRS})
target_link_libraries(sysfs-powerd PRIVATE ${LS2_LIBRARIES} ${CJSON_LIBRARIES} ${GLIB_LIBRARIES})
# -Wextra, minus the one warning HP's own method tables would raise: every
# LSMethod and LSSignal array in the tree is written { "name", function } and
# leaves flags to its zero default, and these follow that.
target_compile_options(sysfs-powerd PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

# usr/sbin, where HP's system services sit in staging. assemble-rootfs.sh finds
# the binary there from the Exec= of the .service file and places it in
# /usr/lib/luna with the rest.
install(TARGETS sysfs-powerd RUNTIME DESTINATION usr/sbin)
