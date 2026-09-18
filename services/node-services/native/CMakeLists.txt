# lunabus.node: luna-service2 for services/node-services. See lunabus.cpp.

cmake_minimum_required(VERSION 3.16)
project(lunabus CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# node_api.h and uv.h come from the pinned node (tools/node-version); build.sh
# passes the directory, as it does for the other addons.
if (NOT NODE_INCLUDE_DIR OR NOT EXISTS "${NODE_INCLUDE_DIR}/node_api.h")
    message(FATAL_ERROR "set NODE_INCLUDE_DIR to the pinned node's include/node")
endif ()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)

set(STAGING "${CMAKE_INSTALL_PREFIX}")

add_library(lunabus MODULE lunabus.cpp)
set_target_properties(lunabus PROPERTIES PREFIX "" SUFFIX ".node")
target_compile_definitions(lunabus PRIVATE NODE_GYP_MODULE_NAME=lunabus NAPI_VERSION=9)
target_compile_options(lunabus PRIVATE -Wall -Wextra)
target_include_directories(lunabus PRIVATE "${NODE_INCLUDE_DIR}" ${GLIB_INCLUDE_DIRS} "${STAGING}/include")
target_link_directories(lunabus PRIVATE "${STAGING}/lib")
target_link_libraries(lunabus PRIVATE ${GLIB_LIBRARIES} luna-service2)
# Node-API and libuv are resolved by the node process that loads the module.
target_link_options(lunabus PRIVATE "LINKER:--unresolved-symbols=ignore-in-object-files")

install(TARGETS lunabus LIBRARY DESTINATION usr/palm/nodejs)
