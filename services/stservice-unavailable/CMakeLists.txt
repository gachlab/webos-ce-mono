# stservice-unavailable: com.palm.stservice (Touch to Share) answered as
# "unavailable". Ours, not HP's: nothing in the CE drop provides it, and this
# machine has no tap sensor or paired phone to share to. Ticket #43 leaves the
# backend undecided but requires the browser and system UI not to hang on a
# missing service, so this answers cleanly -- a failing shareData and a
# succeeding acknowledgement -- and nothing more.
#
# Built by tools/build.sh's "stservice" stage against the staging tree, so
# PKG_CONFIG_PATH finds luna-service2 there. The smallest service here: no GDBus
# client, and no cjson of its own -- it parses no request body -- though cjson is
# still named below because luna-service2 drags its symbols in at link time.
cmake_minimum_required(VERSION 3.16)
project(stservice-unavailable CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(LS2 REQUIRED luna-service2)
# Named explicitly even though this service parses no JSON: luna-service2 itself
# pulls in the json_* symbols, and here staging puts libcjson in lib/ while
# luna-service2 sits in usr/lib/ with an rpath of $ORIGIN:$ORIGIN/.., so without
# naming cjson every json_* that libluna-service2 needs goes unresolved at link
# time. sysfs-powerd and nm-connectionmanager declare cjson for the same reason.
pkg_check_modules(CJSON REQUIRED cjson)

add_executable(stservice-unavailable src/main.cpp)
target_include_directories(stservice-unavailable PRIVATE
    src ${GLIB_INCLUDE_DIRS} ${LS2_INCLUDE_DIRS})
target_link_directories(stservice-unavailable PRIVATE ${LS2_LIBRARY_DIRS} ${CJSON_LIBRARY_DIRS})
target_link_libraries(stservice-unavailable PRIVATE ${LS2_LIBRARIES} ${CJSON_LIBRARIES} ${GLIB_LIBRARIES})
# -Wextra, minus the one warning HP's own method tables would raise: every
# LSMethod array in the tree is written { "name", function } and leaves flags to
# its zero default, and these follow that.
target_compile_options(stservice-unavailable PRIVATE
    -Wall -Wextra -Wno-missing-field-initializers)

# usr/sbin, where HP's system services sit in staging. assemble-rootfs.sh finds
# the binary there from the Exec= of the .service file and places it in
# /usr/lib/luna with the rest.
install(TARGETS stservice-unavailable RUNTIME DESTINATION usr/sbin)
