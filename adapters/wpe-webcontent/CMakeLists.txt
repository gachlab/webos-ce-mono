# WPE WebKit headless view that yields CPU frames for the WebAppMgr paint path.
# Spike for #81 — GObject + WPEPlatform, no WPEQt, no QWebEngine* façade.

cmake_minimum_required(VERSION 3.16)
project(wpe-webcontent LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Gui)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WPE REQUIRED wpe-webkit-2.0 wpe-platform-headless-2.0)

add_library(wpe-webcontent-lib STATIC
    src/headless_view.cpp
)
set_target_properties(wpe-webcontent-lib PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(wpe-webcontent-lib
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE ${WPE_INCLUDE_DIRS}
)
target_compile_options(wpe-webcontent-lib PRIVATE ${WPE_CFLAGS_OTHER})
target_link_libraries(wpe-webcontent-lib
    PUBLIC  Qt6::Gui
    PRIVATE ${WPE_LIBRARIES}
)
