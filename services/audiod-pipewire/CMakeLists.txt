# audiod-pipewire: com.palm.audio for a machine whose audio daemon is PipeWire,
# reached through the PulseAudio client API. Ours, not HP's: nothing in the CE
# drop provides com.palm.audio, so every audio control in the shell is inert.
#
# The PulseAudio *client* library over the PipeWire *daemon* is not a
# contradiction -- it is what PipeWire's own documentation points applications
# at, and the session's existing stream already arrives tagged
# client.api="pipewire-pulse". See src/pulse_backend.h.
#
# Built by tools/build.sh's "audiod" stage against the staging tree, so
# PKG_CONFIG_PATH finds luna-service2 and cjson there; glib and libpulse come
# from the host, as glib already does for sysfs-powerd.
cmake_minimum_required(VERSION 3.16)
project(audiod-pipewire CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(LS2 REQUIRED luna-service2)
# Named explicitly for the same reason sysfs-powerd names it: staging puts
# libcjson in lib/ while luna-service2 sits in usr/lib/ with an rpath of
# $ORIGIN:$ORIGIN/.., so every cJSON_* symbol would otherwise go unresolved.
pkg_check_modules(CJSON REQUIRED cjson)
# The PulseAudio client API and its glib mainloop binding. From the host:
# PipeWire ships libpulse as its Pulse-compatibility client, which is what we
# link -- the daemon is PipeWire, the client API is Pulse.
pkg_check_modules(PULSE REQUIRED libpulse)
pkg_check_modules(PULSE_GLIB REQUIRED libpulse-mainloop-glib)

add_executable(audiod-pipewire
    src/main.cpp
    src/pulse_backend.cpp)
target_include_directories(audiod-pipewire PRIVATE
    src ${GLIB_INCLUDE_DIRS} ${LS2_INCLUDE_DIRS} ${CJSON_INCLUDE_DIRS} ${PULSE_INCLUDE_DIRS})
target_link_directories(audiod-pipewire PRIVATE
    ${LS2_LIBRARY_DIRS} ${CJSON_LIBRARY_DIRS} ${PULSE_LIBRARY_DIRS})
target_link_libraries(audiod-pipewire PRIVATE
    ${LS2_LIBRARIES} ${CJSON_LIBRARIES} ${GLIB_LIBRARIES}
    ${PULSE_LIBRARIES} ${PULSE_GLIB_LIBRARIES})
# -Wextra, minus the one warning HP's own method tables raise: every LSMethod
# array in the tree is written { "name", function } and leaves flags to its zero
# default, and these follow that.
target_compile_options(audiod-pipewire PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

# usr/sbin, where HP's system services sit in staging. assemble-rootfs.sh finds
# the binary there from the Exec= of the .service file and places it in
# /usr/lib/luna with the rest.
install(TARGETS audiod-pipewire RUNTIME DESTINATION usr/sbin)
