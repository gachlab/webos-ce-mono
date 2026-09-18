# qtwebkit-compat: the QtWebKit API WebAppMgr is written against, on top of
# QtWebEngine -- the same idea as adapters/node-v8-shim and qt6-compat.
#
# Pulled in by webappmanager, and by the test that drives it:
#
#   add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../adapters/qtwebkit-compat
#                    ${CMAKE_CURRENT_BINARY_DIR}/qtwebkit-compat)
#   target_link_libraries(<target> PRIVATE qtwebkit-compat-lib)
#
# The target is qtwebkit-compat-lib, not qtwebkit-compat: the test program
# that exercises it is called qtwebkit-compat.
#
# Sources are split by concern (#59). Each concern that other .cpp files call
# has a matching .h (scripts.h, bridge-scheme.h); the public header stays one.

cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Widgets Quick QuickWidgets WebEngineCore WebEngineWidgets)

set(QTWEBKIT_COMPAT_SOURCES
    src/scripts.cpp
    src/bridge-scheme.cpp
    src/page.cpp
    src/frame.cpp
    src/embedded.cpp
    src/input.cpp
)

add_library(qtwebkit-compat-lib STATIC
    include/qtwebkit_compat.h
    src/scripts.h
    src/bridge-scheme.h
    ${QTWEBKIT_COMPAT_SOURCES}
)
set_target_properties(qtwebkit-compat-lib PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON)
target_include_directories(qtwebkit-compat-lib PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_include_directories(qtwebkit-compat-lib PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src")
target_link_libraries(qtwebkit-compat-lib PUBLIC
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Widgets Qt6::Quick Qt6::QuickWidgets
    Qt6::WebEngineCore Qt6::WebEngineWidgets)
