# smartkey-hunspell: com.palm.smartKey answered from libhunspell, for a build
# whose spelling and word-completion comes from the host's dictionaries rather
# than HP's unreleased smartKey. Ours, not HP's: nothing in the CE drop provides
# com.palm.smartKey, and HP never released it as source. hunspell is the
# open-source checker HP itself used.
#
# Built by tools/build.sh's "smartkey" stage against the staging tree, so
# PKG_CONFIG_PATH finds luna-service2 there. Same shape as
# services/stservice-unavailable and services/zeroconf-avahi.
cmake_minimum_required(VERSION 3.16)
project(smartkey-hunspell CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(LS2 REQUIRED luna-service2)
# Named explicitly for the same reason as in nm-connectionmanager: luna-service2
# needs libcjson and cannot find it by itself in staging. The request parsing in
# main.cpp uses it too.
pkg_check_modules(CJSON REQUIRED cjson)
# The spell checker. Taken from the distribution (libhunspell-dev); its
# dictionaries are read from the host at runtime, not vendored.
pkg_check_modules(HUNSPELL REQUIRED hunspell)

add_executable(smartkey-hunspell src/main.cpp src/hunspell_engine.cpp)
target_include_directories(smartkey-hunspell PRIVATE
    src ${GLIB_INCLUDE_DIRS} ${LS2_INCLUDE_DIRS} ${CJSON_INCLUDE_DIRS} ${HUNSPELL_INCLUDE_DIRS})
target_link_directories(smartkey-hunspell PRIVATE
    ${LS2_LIBRARY_DIRS} ${CJSON_LIBRARY_DIRS} ${HUNSPELL_LIBRARY_DIRS})
target_link_libraries(smartkey-hunspell PRIVATE
    ${LS2_LIBRARIES} ${CJSON_LIBRARIES} ${HUNSPELL_LIBRARIES} ${GLIB_LIBRARIES})
# -Wextra minus the one warning HP's own LSMethod tables raise: every method
# array in the tree is { "name", function } and leaves flags to zero.
target_compile_options(smartkey-hunspell PRIVATE
    -Wall -Wextra -Wno-missing-field-initializers)

# usr/sbin, where HP's system services sit in staging; assemble-rootfs.sh finds
# the binary there from the Exec= of the .service file and places it in
# /usr/lib/luna with the rest.
install(TARGETS smartkey-hunspell RUNTIME DESTINATION usr/sbin)
