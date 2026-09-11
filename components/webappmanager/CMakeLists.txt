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

# QtWebKit 5.212 is not a system package: tools/build-third-party.sh installs it
# under staging/qtwebkit. Point CMake at its package configs before looking.
if(NOT DEFINED WEBOS_QTWEBKIT)
    set(WEBOS_QTWEBKIT "${CMAKE_INSTALL_PREFIX}/qtwebkit")
endif()
file(GLOB QTWEBKIT_CMAKE_DIRS "${WEBOS_QTWEBKIT}/lib/*/cmake")
list(APPEND CMAKE_PREFIX_PATH ${QTWEBKIT_CMAKE_DIRS})

# OpenGL comes from the .pro's "contains(CONFIG_BUILD, opengl): QT += opengl".
# It is what brings QGLWidget, which CardWebApp.cpp includes -- and one of the
# 14 files that will need changing if this ever moves to Qt6, where QGLWidget is
# gone in favour of QOpenGLWidget.
find_package(Qt5 REQUIRED COMPONENTS Core Gui Network Widgets Quick OpenGL)

# Asked for as their own packages rather than as Qt5 components: component
# lookup goes through Qt5Config.cmake, which only searches next to itself, so it
# would never see the one under staging.
find_package(Qt5WebKit REQUIRED)
find_package(Qt5WebKitWidgets REQUIRED)
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
    Src/webbase/WindowedWebApp.cpp
)

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

target_compile_options(WebAppMgr PRIVATE
    -fno-rtti -fno-exceptions
    -fvisibility=hidden -fvisibility-inlines-hidden
    -Wall -fpermissive
    -Wno-unused-parameter -Wno-unused-variable -Wno-reorder
    -Wno-missing-field-initializers -Wno-extra
)

target_link_libraries(WebAppMgr PRIVATE
    Qt5::Core Qt5::Gui Qt5::Network Qt5::Widgets Qt5::Quick Qt5::OpenGL
    Qt5::WebKit Qt5::WebKitWidgets
    ${GLIB_LDFLAGS} ${SQLITE_LDFLAGS}
    OpenSSL::SSL OpenSSL::Crypto OpenGL::GL
    -L${WEBOS_STAGING}/lib -L${WEBOS_STAGING}/usr/lib
    # pbnjson_c is listed even though the .pro only names pbnjson_cpp: the C++
    # half declares it as NEEDED, but the linker does not search our -L paths for
    # a dependency's dependencies, so it has to be named.
    cjson LunaSysMgrIpc lunaservice pbnjson_cpp pbnjson_c LunaSysMgrCommon
)

# Found at runtime through LD_LIBRARY_PATH, but an rpath keeps it working when
# the binary is run directly, which is how HP's .pro had it.
set_target_properties(WebAppMgr PROPERTIES
    INSTALL_RPATH "${WEBOS_STAGING}/lib;${WEBOS_STAGING}/usr/lib"
)

install(TARGETS WebAppMgr RUNTIME DESTINATION bin)
