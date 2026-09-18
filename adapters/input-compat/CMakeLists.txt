# input-compat: input modern hardware produces that webOS's event catalogue
# never had, carried across HP's IPC without changing it.
#
# Not a component of its own, and deliberately header-only: the shell and
# WebAppMgr both pack and unpack the same event, and the one place that knows
# which of SysMgrEvent's fields carry what has to be shared between them.
#
#   add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../adapters/input-compat
#                    ${CMAKE_CURRENT_BINARY_DIR}/input-compat)
#   target_link_libraries(<target> PRIVATE input-compat)
#
# The header includes <SysMgrEvent.h>, which both consumers already reach
# through ${WEBOS_STAGING}/usr/include/sysmgr-ipc.

cmake_minimum_required(VERSION 3.16)

add_library(input-compat INTERFACE)
target_include_directories(input-compat INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")
