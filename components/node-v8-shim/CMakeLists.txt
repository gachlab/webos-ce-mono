# The node 0.4 V8 API, on N-API. See README.md.
#
# Builds as a static library the addons link, plus a helper that turns one of
# HP's sources into a loadable .node without changing it.

cmake_minimum_required(VERSION 3.16)
project(node-v8-shim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Where node keeps node_api.h. Found from the node on PATH, so the build follows
# whichever node the rest of the system uses.
if (NOT NODE_INCLUDE_DIR)
    find_program(NODE_EXECUTABLE node REQUIRED)
    get_filename_component(_node_bin "${NODE_EXECUTABLE}" DIRECTORY)
    get_filename_component(_node_root "${_node_bin}" DIRECTORY)
    set(NODE_INCLUDE_DIR "${_node_root}/include/node")
endif ()
if (NOT EXISTS "${NODE_INCLUDE_DIR}/node_api.h")
    message(FATAL_ERROR "no node_api.h under ${NODE_INCLUDE_DIR}")
endif ()
message(STATUS "node headers: ${NODE_INCLUDE_DIR}")

add_library(node-v8-shim STATIC src/v8_shim.cpp src/ev_shim.cpp src/events_shim.cpp)
target_include_directories(node-v8-shim PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include" "${NODE_INCLUDE_DIR}")

# N-API is resolved by the node process that loads the module, so the symbols
# are left undefined on purpose.
target_link_options(node-v8-shim INTERFACE "LINKER:--unresolved-symbols=ignore-in-object-files")

# webos_node_addon(<name> <sources...>)
#
# Produces <name>.node from sources written against node 0.4, linking the shim
# and the entry point that bridges init() to NAPI_MODULE.
function(webos_node_addon NAME)
    add_library(${NAME} MODULE ${ARGN} "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/src/module_entry.cpp")
    target_link_libraries(${NAME} PRIVATE node-v8-shim)
    target_compile_definitions(${NAME} PRIVATE NODE_GYP_MODULE_NAME=${NAME})
    set_target_properties(${NAME} PROPERTIES PREFIX "" SUFFIX ".node")

    # -Bsymbolic, and it is not optional.
    #
    # The shim defines v8::FunctionTemplate, v8::String and the rest, and so does
    # the node binary that loads it -- the real V8, with the same mangled names,
    # already in the global symbol table. Without this the dynamic linker
    # resolves the addon's own calls to node's V8, which then runs on objects
    # that are not V8 objects at all.
    #
    # It is not a link error and not a warning. The first symptom is a segfault
    # inside v8::FunctionTemplate::InstanceTemplate with the addon on the stack
    # below it, which is exactly how it was found.
    target_link_options(${NAME} PRIVATE "LINKER:-Bsymbolic")
endfunction()
