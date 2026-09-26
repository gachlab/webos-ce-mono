# dma-buf window buffers for the WebAppMgr ↔ LunaSysMgr present path (#79).
# GBM create/map/export + EGLImage import. No HP types here.

cmake_minimum_required(VERSION 3.16)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GBM REQUIRED gbm)
pkg_check_modules(DRM REQUIRED libdrm)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLESV2 REQUIRED glesv2)

add_library(dmabuf-window STATIC
    src/device.cpp
    src/frame.cpp
    src/egl_import.cpp
    src/registry.cpp)
set_target_properties(dmabuf-window PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF)
target_include_directories(dmabuf-window PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include")
target_include_directories(dmabuf-window PRIVATE
    ${GBM_INCLUDE_DIRS}
    ${DRM_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GLESV2_INCLUDE_DIRS})
target_link_libraries(dmabuf-window PUBLIC
    ${GBM_LIBRARIES}
    ${DRM_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLESV2_LIBRARIES})
