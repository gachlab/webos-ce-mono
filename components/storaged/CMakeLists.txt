# storaged: com.palm.storage, for a machine with no USB gadget and no partitions
# of its own. Ours, not HP's -- the CE drop ships nothing for this name, and Open
# webOS's storaged is 1900 lines of C against udev, nyx and a phone's USB gadget.
# What is HP's is the API its callers in this tree use.
#
# Built by tools/build.sh's "storaged" stage against the staging tree, the same
# shape as components/sysfs-powerd and components/nm-connectionmanager.
cmake_minimum_required(VERSION 3.16)
project(storaged CXX)

# 17 for std::filesystem, which is how the erase walks and removes.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(LS2 REQUIRED luna-service2)
# Named explicitly for the same reason as in nm-connectionmanager: luna-service2
# needs cjson and cannot find it from staging by itself. The erase also parses
# the download history with it.
pkg_check_modules(CJSON REQUIRED cjson)

add_executable(storaged src/main.cpp src/erase.cpp)
target_include_directories(storaged PRIVATE
    src ${GLIB_INCLUDE_DIRS} ${LS2_INCLUDE_DIRS} ${CJSON_INCLUDE_DIRS})
target_link_directories(storaged PRIVATE ${LS2_LIBRARY_DIRS} ${CJSON_LIBRARY_DIRS})
target_link_libraries(storaged PRIVATE ${LS2_LIBRARIES} ${CJSON_LIBRARIES} ${GLIB_LIBRARIES})
# -Wextra, minus the warning every LSMethod table in this tree would raise.
target_compile_options(storaged PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

install(TARGETS storaged RUNTIME DESTINATION usr/sbin)
