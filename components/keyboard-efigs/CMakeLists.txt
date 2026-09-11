# keyboard-efigs, translated from keyboard-efigs.pro + common.pri.
#
# HP's .pro files are kept: they are the original and they document the device
# and emulator targets. This covers the desktop target only.
#
# Two Qt plugins from one shared set of sources, which is what the .pro's
# TEMPLATE = subdirs plus a shared common.pri expresses.

cmake_minimum_required(VERSION 3.16)
project(keyboard-efigs CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Qt 5 or 6. Qt 5 is the build that runs today; 6 is where the stack is moving,
# built in its own tree by tools/build-cmake.sh (WEBOS_QT=6). Both provide the
# versionless Qt:: targets used below.
set(WEBOS_QT_MAJOR 5 CACHE STRING "Qt major version to build against: 5 or 6")
find_package(Qt${WEBOS_QT_MAJOR} REQUIRED COMPONENTS Core Gui Widgets)
# Under Qt 6, the adapters for what it removed (components/qt6-compat).
if (WEBOS_QT_MAJOR GREATER_EQUAL 6)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../qt6-compat ${CMAKE_CURRENT_BINARY_DIR}/qt6-compat)
    set(WEBOS_QT6_COMPAT qt6compat)
endif ()
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0 gthread-2.0)

set(WEBOS_STAGING "${CMAKE_INSTALL_PREFIX}")

# The shared half of common.pri.
set(COMMON_SOURCES
    src/CandidateBar.cpp
    src/CandidateBarRemote.cpp
    src/GlyphCache.cpp
    src/IMEPixmap.cpp
    src/JSONUtils.cpp
    src/KeyLocationRecorder.cpp
    src/PalmIMEHelpers.cpp
    src/ShortcutsHandler.cpp

    # Q_OBJECT headers, listed so AUTOMOC sees them: it pairs X.cpp with an X.h
    # in the same directory, and here the headers are in include/ while the
    # sources are in src/.
    include/CandidateBar.h
)

# common.pri also puts the ime headers from staging into HEADERS, so qmake ran
# moc over them. They define IMEDataInterface and the IMEData_* templates, and
# nothing else in the tree provides their meta-objects -- without this the
# plugins link with qt_metacall undefined.
set(IME_HEADERS
    ${WEBOS_STAGING}/include/ime/IMEData.h
    ${WEBOS_STAGING}/include/ime/IMEDataInterface.h
    ${WEBOS_STAGING}/include/ime/InputMethod.h
    ${WEBOS_STAGING}/include/ime/VirtualKeyboard.h
)

# One plugin per keyboard layout. Everything else is identical.
foreach(VARIANT phone tablet)
    string(TOUPPER ${VARIANT} UPPER)
    set(TGT keyboard-efigs-${VARIANT})

    if(VARIANT STREQUAL phone)
        set(VARIANT_SOURCES src/PhoneKeyboard.cpp src/PhoneKeymap.cpp
                            include/PhoneKeyboard.h)
    else()
        set(VARIANT_SOURCES src/TabletKeyboard.cpp src/TabletKeymap.cpp
                            include/TabletKeyboard.h)
    endif()

    add_library(${TGT} MODULE ${COMMON_SOURCES} ${VARIANT_SOURCES} ${IME_HEADERS})

    target_include_directories(${TGT} PRIVATE
        . include src
        ${WEBOS_STAGING}/include
        ${WEBOS_STAGING}/include/ime
        ${WEBOS_STAGING}/include/luna-service2
        ${WEBOS_STAGING}/usr/include
        ${GLIB_INCLUDE_DIRS}
    )

    # CONFIG += no_keywords. webOS uses "signal" as an identifier, which clashes
    # with Qt's signals/slots macros.
    target_compile_definitions(${TGT} PRIVATE QT_NO_KEYWORDS TARGET_DESKTOP)

    target_compile_options(${TGT} PRIVATE
        -fno-rtti -fno-exceptions
        -fvisibility=hidden -fvisibility-inlines-hidden
        -Wall -fpermissive
        -Wno-unused-parameter -Wno-unused-variable -Wno-reorder
        -Wno-missing-field-initializers -Wno-extra
    )

    target_link_libraries(${TGT} PRIVATE
        Qt::Core Qt::Gui Qt::Widgets ${WEBOS_QT6_COMPAT}
        ${GLIB_LDFLAGS}
        -L${WEBOS_STAGING}/lib -L${WEBOS_STAGING}/usr/lib
        pbnjson_cpp lunaservice
    )

    # The .pro installs into $ROOTFS/usr/lib/luna, not staging: these are runtime
    # plugins, loaded by IMEManager from that path, not link-time libraries.
    install(TARGETS ${TGT} LIBRARY DESTINATION "${WEBOS_ROOTFS}/usr/lib/luna")
endforeach()
