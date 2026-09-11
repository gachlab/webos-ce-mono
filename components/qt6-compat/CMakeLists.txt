# qt6-compat: what Qt 6 removed and webOS's code still uses, rebuilt on top of
# what Qt 6 has -- the same idea as components/node-v8-shim, for Qt.
#
# Not a component of its own: the components that use Qt pull it in.
#
#   add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../qt6-compat
#                    ${CMAKE_CURRENT_BINARY_DIR}/qt6-compat)
#   target_link_libraries(<target> PRIVATE qt6compat)      # forced include only
#   target_link_libraries(<target> PRIVATE qt6compat-gl)   # plus the QGL* classes

cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Core Gui OpenGL OpenGLWidgets)

# include/qt6compat.h goes into every C++ file of whoever links this, so HP's
# sources get the removed free functions back without being edited.
add_library(qt6compat INTERFACE)
target_include_directories(qt6compat INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")
target_compile_options(qt6compat INTERFACE
    "SHELL:-include ${CMAKE_CURRENT_LIST_DIR}/include/qt6compat.h")

# QGLContext, QGLFormat, QGLWidget and QGLFramebufferObject. Static and PIC so it
# can go into the shell's executable as well as into a plugin.
add_library(qt6compat-gl STATIC src/qglcompat.cpp)
set_target_properties(qt6compat-gl PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC OFF)
target_link_libraries(qt6compat-gl PUBLIC
    qt6compat Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets)
