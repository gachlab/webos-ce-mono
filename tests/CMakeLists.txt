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

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Qml Quick)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)

enable_testing()

set(SYSMGR "${CMAKE_CURRENT_SOURCE_DIR}/../components/luna-sysmgr")

# --- input plumbing, no webOS ------------------------------------------------
foreach (t touch-in-qt5 eater-synthesis-qt5 home-gesture-qt5)
    add_executable(${t} ${t}.cpp)
    target_link_libraries(${t} PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)
    add_test(NAME ${t} COMMAND ${t} -platform offscreen)
endforeach ()

# --- every .qml file must load under QtQuick 2 -------------------------------
add_executable(qml-loads-qt5 qml-loads-qt5.cpp)
target_link_libraries(qml-loads-qt5 PRIVATE Qt5::Core Qt5::Gui Qt5::Qml Qt5::Quick)
add_test(NAME qml-loads-luna-sysmgr
         COMMAND qml-loads-qt5 -platform offscreen "${SYSMGR}/uiComponents")
add_test(NAME qml-loads-luna-sysmgr-ce
         COMMAND qml-loads-qt5 -platform offscreen
                 "${CMAKE_CURRENT_SOURCE_DIR}/../components/luna-sysmgr-ce/uiComponents")

# --- undefined identifiers inside QML functions -------------------------------
# Only the files that declare functions are worth calling; the rest are pure
# declarative markup and qml-loads already covers them.
add_executable(qml-functions-qt5 qml-functions-qt5.cpp)
target_link_libraries(qml-functions-qt5 PRIVATE Qt5::Core Qt5::Gui Qt5::Qml Qt5::Quick)
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
    "${CMAKE_CURRENT_SOURCE_DIR}/../build-modern/staging/include/luna-sysmgr-common")
target_link_libraries(qml-scene-item-qt5 PRIVATE
    Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Qml Qt5::Quick ${GLIB_LIBRARIES})
add_test(NAME qml-scene-item COMMAND qml-scene-item-qt5 -platform offscreen)

# --- the run script's process detection ---------------------------------------
add_test(NAME long-process-name
         COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/long-process-name.sh")

# --- pbnjson ------------------------------------------------------------------
find_library(PBNJSON_C NAMES pbnjson_c
             PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../build-modern/staging/lib" NO_DEFAULT_PATH)
if (PBNJSON_C)
    add_executable(pbnjson-smoke pbnjson-smoke.c)
    target_include_directories(pbnjson-smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../build-modern/staging/include")
    target_link_libraries(pbnjson-smoke PRIVATE ${PBNJSON_C})
    add_test(NAME pbnjson-smoke COMMAND pbnjson-smoke)
else ()
    message(STATUS "pbnjson not staged yet; skipping pbnjson-smoke")
endif ()
