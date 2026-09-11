# LunaSysMgrCommon, translated from sysmgr-common.pro.
#
# HP's .pro is kept next to this file: it is the original, it documents the
# device and emulator targets we do not build, and it is the reference to diff
# against. This covers the desktop target only -- the linux-g++ branch of the
# .pro, which is what desktop.pri configures.
#
# Two things the .pro does that are not obvious here:
#
#  - VPATH. qmake resolves bare file names against a search path; CMake wants
#    real paths, so the sources below are spelled out. Same set, same order.
#  - Src/nyx/nyx.pri. Its linux-g++ branch is EMPTY, so on desktop none of the
#    nyx sources are built. They are the hardware layer and only apply to the
#    device target.

cmake_minimum_required(VERSION 3.16)
project(LunaSysMgrCommon CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)          # replaces qmake's implicit moc pass
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Qt 5 or 6. Qt 5 is the build that runs today; 6 is where the stack is moving,
# built in its own tree by tools/build-cmake.sh (WEBOS_QT=6). Both provide the
# versionless Qt:: targets used below.
set(WEBOS_QT_MAJOR 5 CACHE STRING "Qt major version to build against: 5 or 6")
find_package(Qt${WEBOS_QT_MAJOR} REQUIRED COMPONENTS Core Gui Network Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0 gthread-2.0)
# The library calls sqlite3 itself. Qt 5 builds never noticed it was not linked:
# QtWebKit brought libsqlite3 into every executable that used this library. On
# Qt 6 nothing does, and WebAppMgr failed to link on sqlite3_open & co.
pkg_check_modules(SQLITE REQUIRED sqlite3)

add_library(LunaSysMgrCommon SHARED
    Src/base/application/ApplicationDescriptionBase.cpp
    Src/core/AsyncCaller.cpp
    Src/base/hosts/HostBase.cpp
    Src/base/JSONUtils.cpp
    Src/base/settings/LocalePreferences.cpp
    Src/base/Localization.cpp
    Src/base/Logging.cpp
    Src/core/Mutex.cpp
    Src/base/settings/Settings.cpp
    Src/core/SingletonTimer.cpp
    Src/core/TaskBase.cpp
    Src/core/Timer.cpp
    Src/base/Utils.cpp
    Src/base/WindowProperties.cpp
    # from desktop.pri
    Src/base/hosts/HostQtDesktop.cpp
    Src/base/gesture/FlickGestureRecognizer.cpp

    # Every Q_OBJECT header, listed so AUTOMOC sees them.
    #
    # qmake ran moc over everything in HEADERS. AUTOMOC instead pairs X.cpp with
    # an X.h in the SAME directory, and here the headers live in include/ while
    # the sources live under Src/. Nothing pairs, so their meta-objects would go
    # missing silently -- the library builds, and the link fails later in
    # whoever uses it, with qt_metacall undefined.
    #
    # NyxSensorConnector.h and HostArm.h are the device target's and are not
    # built here: Src/nyx/nyx.pri leaves its linux-g++ branch empty.
    include/HostBase.h
    include/LocalePreferences.h
    include/ProcessKiller.h
)

# Where the rest of webOS installs its headers. desktop.pri points these at
# $LUNA_STAGING; here they follow CMAKE_INSTALL_PREFIX, which is the same place.
# cjson is reached as <json.h>, so its own directory has to be on the path.
set(WEBOS_STAGING "${CMAKE_INSTALL_PREFIX}")

# qmake's VPATH, which doubles as INCLUDEPATH ("INCLUDEPATH += $$VPATH").
target_include_directories(LunaSysMgrCommon PUBLIC
    Src Src/base Src/base/application Src/base/hosts Src/base/settings
    Src/base/gesture Src/core include
    ${WEBOS_STAGING}/include
    ${WEBOS_STAGING}/include/cjson
    ${WEBOS_STAGING}/include/luna-service2
    ${WEBOS_STAGING}/usr/include
    ${WEBOS_STAGING}/usr/include/sysmgr-ipc
    ${GLIB_INCLUDE_DIRS}
)

target_compile_definitions(LunaSysMgrCommon PRIVATE
    QT_WEBOS
    SHIPPING_VERSION=0
    TARGET_DESKTOP        # from desktop.pri
    FIX_FOR_QT
)

# Same flags as the .pro. The -Wno-* set is HP's: without it the build drowns in
# warnings from 2012 code, and they are not what we are looking for.
target_compile_options(LunaSysMgrCommon PRIVATE
    -fno-rtti -fno-exceptions -Wall -fpermissive
    -Wno-unused-parameter -Wno-unused-variable -Wno-reorder
    -Wno-missing-field-initializers -Wno-extra
)

target_link_libraries(LunaSysMgrCommon PUBLIC
    Qt::Core Qt::Gui Qt::Network Qt::Widgets
    ${GLIB_LDFLAGS}
    ${SQLITE_LDFLAGS}
)

# Under Qt 6, the adapters for what it removed (components/qt6-compat).
if (WEBOS_QT_MAJOR GREATER_EQUAL 6)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../qt6-compat ${CMAKE_CURRENT_BINARY_DIR}/qt6-compat)
    target_link_libraries(LunaSysMgrCommon PRIVATE qt6compat)
endif ()

# qmake gives Qt libraries a version by default: libLunaSysMgrCommon.so.1.0.0
# with soname .so.1. Everything already built links against that soname, so it
# has to match or the consumers keep loading the old file.
set_target_properties(LunaSysMgrCommon PROPERTIES VERSION 1.0.0 SOVERSION 1)

# The .pro installs the library into $LUNA_STAGING/lib and build.sh copies the
# headers into staging/include/luna-sysmgr-common separately, as HP's script did.
install(TARGETS LunaSysMgrCommon LIBRARY DESTINATION lib)
install(DIRECTORY include/ DESTINATION include/luna-sysmgr-common
        FILES_MATCHING PATTERN "*.h")
