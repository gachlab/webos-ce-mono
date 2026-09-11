# The tests. Each is a standalone program that runs headless and returns 0 or 1,
# so they can be run by hand as easily as by ctest:
#
#   cmake -S tests -B build-modern/tests && cmake --build build-modern/tests
#   ctest --test-dir build-modern/tests --output-on-failure
#
# They exist so the shell does not have to be brought up to find out whether
# something works. Anything learned the hard way by launching LunaSysMgr belongs
# here instead.

cmake_minimum_required(VERSION 3.16)
project(webos-tests CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt 5 or 6. Qt 5 is the build that runs today; 6 is where the stack is moving,
# built in its own tree by tools/build-cmake.sh (WEBOS_QT=6). Both provide the
# versionless Qt:: targets used below.
set(WEBOS_QT_MAJOR 5 CACHE STRING "Qt major version to build against: 5 or 6")
find_package(Qt${WEBOS_QT_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Qml Quick)
# The staging tree the tests read webOS headers and libraries from.
set(WEBOS_STAGING "${CMAKE_CURRENT_SOURCE_DIR}/../build-modern/staging"
    CACHE PATH "webOS staging tree matching WEBOS_QT_MAJOR")
# Under Qt 6, touch points are built through QMutableEventPoint (touch-events.h,
# and MouseToTouch and QmlSceneItem, which the tests compile): private API.
if (WEBOS_QT_MAJOR GREATER_EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate Test)
    set(WEBOS_QT6_TEST_MODULES Qt::GuiPrivate)
    # qml-scene-item injects its touches through QTest under Qt 6.
    set(WEBOS_QT6_QTEST Qt::Test)
endif ()
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)

enable_testing()

set(SYSMGR "${CMAKE_CURRENT_SOURCE_DIR}/../components/luna-sysmgr")

# --- input plumbing, no webOS ------------------------------------------------
foreach (t touch-in-qt5 eater-synthesis-qt5 home-gesture-qt5)
    add_executable(${t} ${t}.cpp)
    target_link_libraries(${t} PRIVATE Qt::Core Qt::Gui Qt::Widgets ${WEBOS_QT6_TEST_MODULES})
    add_test(NAME ${t} COMMAND ${t} -platform offscreen)
endforeach ()

# --- which coordinate space a touch point arrives in --------------------------
add_executable(touch-coordinate-spaces-qt5 touch-coordinate-spaces-qt5.cpp)
target_link_libraries(touch-coordinate-spaces-qt5 PRIVATE Qt::Core Qt::Gui Qt::Widgets ${WEBOS_QT6_TEST_MODULES})
add_test(NAME touch-coordinate-spaces
         COMMAND touch-coordinate-spaces-qt5 -platform offscreen)

# --- mouse to touch: a drag has to look like a drag -------------------------
add_executable(mouse-to-touch-qt5
    mouse-to-touch-qt5.cpp
    ${SYSMGR}/Src/base/MouseToTouch.cpp
)
target_include_directories(mouse-to-touch-qt5 PRIVATE ${SYSMGR}/Src/base)
target_link_libraries(mouse-to-touch-qt5 PRIVATE Qt::Core Qt::Gui ${WEBOS_QT6_TEST_MODULES})
add_test(NAME mouse-to-touch COMMAND mouse-to-touch-qt5 -platform offscreen)

# --- every .qml file must load under QtQuick 2 -------------------------------
add_executable(qml-loads-qt5 qml-loads-qt5.cpp)
target_link_libraries(qml-loads-qt5 PRIVATE Qt::Core Qt::Gui Qt::Qml Qt::Quick)
add_test(NAME qml-loads-luna-sysmgr
         COMMAND qml-loads-qt5 -platform offscreen "${SYSMGR}/uiComponents")
# luna-sysmgr-ce is kept for reference and not built; its QML is only checked
# against the Qt 5 it was ported to, not carried forward to Qt 6.
if (WEBOS_QT_MAJOR EQUAL 5)
    add_test(NAME qml-loads-luna-sysmgr-ce
             COMMAND qml-loads-qt5 -platform offscreen
                     "${CMAKE_CURRENT_SOURCE_DIR}/../components/luna-sysmgr-ce/uiComponents")
endif ()

# --- undefined identifiers inside QML functions -------------------------------
# Only the files that declare functions are worth calling; the rest are pure
# declarative markup and qml-loads already covers them.
add_executable(qml-functions-qt5 qml-functions-qt5.cpp)
target_link_libraries(qml-functions-qt5 PRIVATE Qt::Core Qt::Gui Qt::Qml Qt::Quick)
file(GLOB_RECURSE QML_WITH_FUNCTIONS "${SYSMGR}/uiComponents/*.qml")
add_test(NAME qml-functions
         COMMAND qml-functions-qt5 -platform offscreen ${QML_WITH_FUNCTIONS})

# --- QmlSceneItem's contract -------------------------------------------------
# Compiles the adapter straight out of the shell's tree: it is the thing under
# test, and a copy here would drift.
add_executable(qml-scene-item-qt5
    qml-scene-item-qt5.cpp
    ${SYSMGR}/Src/base/QmlSceneItem.cpp
)
# QmlSceneItem.cpp opens with the shell's Common.h, which luna-sysmgr-common
# installs into staging.
target_include_directories(qml-scene-item-qt5 PRIVATE
    ${SYSMGR}/Src ${SYSMGR}/Src/base ${GLIB_INCLUDE_DIRS}
    "${WEBOS_STAGING}/include/luna-sysmgr-common")
target_link_libraries(qml-scene-item-qt5 PRIVATE
    Qt::Core Qt::Gui Qt::Widgets Qt::Qml Qt::Quick ${GLIB_LIBRARIES} ${WEBOS_QT6_TEST_MODULES} ${WEBOS_QT6_QTEST})
add_test(NAME qml-scene-item COMMAND qml-scene-item-qt5 -platform offscreen)

# --- the run script's process detection ---------------------------------------
add_test(NAME long-process-name
         COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/long-process-name.sh")

# --- HP's node 0.4 addons, unmodified, in a modern node ----------------------
add_test(NAME node-shim COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/node-shim/run.sh")

# --- static sweeps over the tree ---------------------------------------------
# Both grew out of a real bug in HP's Qt 5 desktop branch, which never ran on a
# device: a scene position handed to mapFromParent, and an else-if that repeated
# an earlier condition so a whole handler was unreachable.
add_test(NAME check-coordinate-spaces
         COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/../tools/check-coordinate-spaces.py"
                 "${CMAKE_CURRENT_SOURCE_DIR}/../components")
add_test(NAME check-touch-opt-in
         COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/../tools/check-touch-opt-in.py"
                 "${CMAKE_CURRENT_SOURCE_DIR}/../components")
add_test(NAME check-touch-vs-mouse
         COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/../tools/check-touch-vs-mouse.py"
                 "${CMAKE_CURRENT_SOURCE_DIR}/../components")
add_test(NAME check-dead-branches
         COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/../tools/check-dead-branches.py"
                 "${CMAKE_CURRENT_SOURCE_DIR}/../components")

# --- pbnjson ------------------------------------------------------------------
find_library(PBNJSON_C NAMES pbnjson_c
             PATHS "${WEBOS_STAGING}/lib" NO_DEFAULT_PATH)
if (PBNJSON_C)
    add_executable(pbnjson-smoke pbnjson-smoke.c)
    target_include_directories(pbnjson-smoke PRIVATE
        "${WEBOS_STAGING}/include")
    target_link_libraries(pbnjson-smoke PRIVATE ${PBNJSON_C})
    add_test(NAME pbnjson-smoke COMMAND pbnjson-smoke)
else ()
    message(STATUS "pbnjson not staged yet; skipping pbnjson-smoke")
endif ()
