# wpewebkit-compat: QtWebKit-shaped API on WPE WebKit (#81).
# Parallel to qtwebkit-compat (QtWebEngine). Same class names; separate -I path.

cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WPE REQUIRED wpe-webkit-2.0 wpe-platform-headless-2.0)

if(NOT TARGET wpe-webcontent-lib)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../wpe-webcontent"
                     "${CMAKE_CURRENT_BINARY_DIR}/wpe-webcontent-lib")
endif()

add_library(wpewebkit-compat-lib STATIC
    include/wpewebkit_compat.h
    src/page_engine.h
    src/bridge-scheme.h
    src/scripts.h
    src/page.cpp
    src/frame.cpp
    src/input.cpp
    src/bridge-scheme.cpp
    src/scripts.cpp
)
set_target_properties(wpewebkit-compat-lib PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(wpewebkit-compat-lib
    PUBLIC  "${CMAKE_CURRENT_LIST_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src"
            ${WPE_INCLUDE_DIRS}
)
target_compile_options(wpewebkit-compat-lib PRIVATE ${WPE_CFLAGS_OTHER})
target_link_libraries(wpewebkit-compat-lib
    PUBLIC  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network wpe-webcontent-lib
    PRIVATE ${WPE_LIBRARIES}
)
