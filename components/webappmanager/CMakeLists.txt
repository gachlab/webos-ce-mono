# webappmanager (WebAppMgr), translated from webappmgr.pro.
#
# HP's .pro is kept: it is the original and it documents the device and emulator
# targets. This covers the desktop target only -- its linux-g++ branch.
#
# This is the component that pulls in QtWebKit, so it is also what has to be
# revisited first if the stack ever moves to Qt6 and the maintained QtWebKit
# fork: that one ships CMake package configs and no qmake module, which is why
# this migration is a prerequisite rather than a nicety.

cmake_minimum_required(VERSION 3.16)
project(webappmanager CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(WEBOS_STAGING "${CMAKE_INSTALL_PREFIX}")

# OpenGL comes from the .pro's "contains(CONFIG_BUILD, opengl): QT += opengl".
# It is what brings QGLWidget, which CardWebApp.cpp includes; Qt 6 replaced it
# with QOpenGLWidget, and adapters/qt6-compat puts QGLWidget back on top of it.
#
# There is no QtWebKit here: the API WebAppMgr is written against comes from
# adapters/qtwebkit-compat (QtWebEngine) or adapters/wpewebkit-compat (WPE).
# Switch with -DWEBOS_WEB_ENGINE=wpe|qtwebengine (default qtwebengine).
set(WEBOS_WEB_ENGINE "qtwebengine" CACHE STRING "Web engine behind the QtWebKit API: qtwebengine|wpe")
set_property(CACHE WEBOS_WEB_ENGINE PROPERTY STRINGS qtwebengine wpe)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Widgets Quick OpenGL OpenGLWidgets Positioning)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../adapters/qt6-compat ${CMAKE_CURRENT_BINARY_DIR}/qt6-compat)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../adapters/input-compat ${CMAKE_CURRENT_BINARY_DIR}/input-compat)

if(WEBOS_WEB_ENGINE STREQUAL "wpe")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WPE REQUIRED wpe-webkit-2.0 wpe-platform-headless-2.0)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../adapters/wpewebkit-compat
                     ${CMAKE_CURRENT_BINARY_DIR}/wpewebkit-compat)
    set(WEBOS_WEBKIT_COMPAT_LIB wpewebkit-compat-lib)
    set(WEBOS_WEB_ENGINE_IS_WPE TRUE)
    message(STATUS "WebAppMgr web engine: WPE WebKit (adapters/wpewebkit-compat)")
elseif(WEBOS_WEB_ENGINE STREQUAL "qtwebengine")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../adapters/qtwebkit-compat
                     ${CMAKE_CURRENT_BINARY_DIR}/qtwebkit-compat)
    set(WEBOS_WEBKIT_COMPAT_LIB qtwebkit-compat-lib)
    set(WEBOS_WEB_ENGINE_IS_WPE FALSE)
    message(STATUS "WebAppMgr web engine: QtWebEngine (adapters/qtwebkit-compat)")
else()
    message(FATAL_ERROR "WEBOS_WEB_ENGINE must be qtwebengine or wpe (got '${WEBOS_WEB_ENGINE}')")
endif()

set(WEBOS_WAM_QT_LIBS Qt::Core Qt::Gui Qt::Network Qt::Widgets Qt::Quick Qt::OpenGL
    Qt::OpenGLWidgets Qt::Positioning qt6compat-gl ${WEBOS_WEBKIT_COMPAT_LIB} input-compat)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0 gthread-2.0)
pkg_check_modules(SQLITE REQUIRED sqlite3)
find_package(OpenSSL REQUIRED)
find_package(OpenGL REQUIRED)   # CardWebApp.cpp calls glFlush directly

add_executable(WebAppMgr
    Src/Main.cpp
    Src/base/BackupManager.cpp
    Src/base/EventReporter.cpp
    Src/base/KeyboardMapping.cpp
    Src/base/WebKitEventListener.cpp
    Src/base/application/ApplicationDescription.cpp
    Src/base/settings/DeviceInfo.cpp
    Src/core/KeywordMap.cpp
    Src/core/SyncTask.cpp
    Src/lunaui/WebAppFactoryLuna.cpp
    Src/lunaui/cards/CardWebApp.cpp
    Src/lunaui/dock/DockWebApp.cpp
    Src/lunaui/notifications/AlertWebApp.cpp
    Src/lunaui/notifications/BannerMessageEventFactory.cpp
    Src/lunaui/notifications/DashboardWebApp.cpp
    Src/minimalui/WebAppFactoryMinimal.cpp
    Src/webbase/BrowserViewAdapter.cpp
    Src/webbase/GeolocationAdapter.cpp
    Src/webbase/GeolocationBus.cpp
    Src/webbase/NetworkAppProxyAdapter.cpp
    Src/webbase/NetworkAppProxyBus.cpp
    Src/webbase/MemoryWatcher.cpp
    Src/webbase/PalmServiceBridgeAdapter.cpp
    Src/webbase/PalmSystem.cpp
    Src/webbase/ProcessManager.cpp
    Src/webbase/RemoteWindowData.cpp
    Src/webbase/RemoteWindowDataOpenGLQt.cpp
    Src/webbase/RemoteWindowDataSoftwareQt.cpp
    Src/webbase/SysMgrWebBridge.cpp
    Src/webbase/WebAppBase.cpp
    Src/webbase/WebAppCache.cpp
    Src/webbase/WebAppDeferredUpdateHandler.cpp
    Src/webbase/WebAppFactory.cpp
    Src/webbase/WebAppManager.cpp
    Src/webbase/WheelDelivery.cpp
    Src/webbase/HoverDelivery.cpp
    Src/webbase/WindowedWebApp.cpp
)

# A Qt Positioning plugin built into the executable (see the file).
set_source_files_properties(Src/webbase/GeolocationAdapter.cpp PROPERTIES COMPILE_DEFINITIONS QT_STATICPLUGIN)

# qmake's VPATH, which doubles as INCLUDEPATH ("INCLUDEPATH = $$VPATH").
target_include_directories(WebAppMgr PRIVATE
    Src
    Src/base
    Src/base/application
    Src/base/hosts
    Src/base/gesture
    Src/base/windowdata
    Src/base/settings
    Src/core
    Src/sound
    Src/webbase
    Src/lunaui
    Src/lunaui/cards
    Src/lunaui/notifications
    Src/lunaui/emergency
    Src/lunaui/dock
    Src/minimalui
    Src/lunaui/status-bar
    ${CMAKE_CURRENT_SOURCE_DIR}/../../services/nm-connectionmanager/src
    ${WEBOS_STAGING}/include
    ${WEBOS_STAGING}/include/cjson
    ${WEBOS_STAGING}/include/luna-service2
    ${WEBOS_STAGING}/include/luna-sysmgr-common
    ${WEBOS_STAGING}/include/ime
    ${WEBOS_STAGING}/usr/include
    ${WEBOS_STAGING}/usr/include/sysmgr-ipc
    ${GLIB_INCLUDE_DIRS}
)

target_compile_definitions(WebAppMgr PRIVATE
    QT_WEBOS
    SHIPPING_VERSION=0
    TARGET_DESKTOP
    HAVE_OPENGL
    P_BACKEND=P_BACKEND_SOFT
    QT_NO_KEYWORDS          # CONFIG += no_keywords: webOS uses "signal" as a name
    FIX_FOR_QT
)
if(WEBOS_WEB_ENGINE_IS_WPE)
    target_compile_definitions(WebAppMgr PRIVATE WEBOS_WEB_ENGINE_WPE)
endif()

target_compile_options(WebAppMgr PRIVATE
    -fno-rtti -fno-exceptions
    -fvisibility=hidden -fvisibility-inlines-hidden
    -Wall -fpermissive
    -Wno-unused-parameter -Wno-unused-variable -Wno-reorder
    -Wno-missing-field-initializers -Wno-extra
)

target_link_libraries(WebAppMgr PRIVATE
    ${WEBOS_WAM_QT_LIBS}
    ${GLIB_LDFLAGS} ${SQLITE_LDFLAGS}
    OpenSSL::SSL OpenSSL::Crypto OpenGL::GL
    -L${WEBOS_STAGING}/lib -L${WEBOS_STAGING}/usr/lib
    # pbnjson_c is listed even though the .pro only names pbnjson_cpp: the C++
    # half declares it as NEEDED, but the linker does not search our -L paths for
    # a dependency's dependencies, so it has to be named.
    cjson LunaSysMgrIpc lunaservice pbnjson_cpp pbnjson_c LunaSysMgrCommon
)

# The rpath is set for every component at once by tools/build.sh, as
# $ORIGIN/.. -- relative to the binary, so the rootfs can be unpacked anywhere.
# This used to be an absolute ${WEBOS_STAGING} path, which put one developer's
# home directory inside the shipped executable; a per-target property here would
# override the global one.

install(TARGETS WebAppMgr RUNTIME DESTINATION bin)
