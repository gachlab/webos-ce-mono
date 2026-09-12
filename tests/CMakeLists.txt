# The tests. Each is a standalone program that runs headless and returns 0 or 1,
# so they can be run by hand as easily as by ctest:
#
#   cmake -S tests -B build/tests && cmake --build build/tests
#   ctest --test-dir build/tests --output-on-failure
#
# They exist so the shell does not have to be brought up to find out whether
# something works. Anything learned the hard way by launching LunaSysMgr belongs
# here instead.

cmake_minimum_required(VERSION 3.16)
project(webos-tests CXX C)

# Debug unless told otherwise, as tools/build.sh builds the components. Qt's own
# checks (Q_ASSERT, and the receiver check on member-function connections) only
# exist then, and a test that runs without them can pass while the shell
# crashes: WebAppMgr segfaulted in one of those checks.
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif ()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Qml Quick Test)
# Touch points are built through QMutableEventPoint -- here in touch-events.h,
# and in MouseToTouch and QmlSceneItem, which these tests compile: private API.
find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)

# The staging tree the tests read webOS headers and libraries from.
set(WEBOS_STAGING "${CMAKE_CURRENT_SOURCE_DIR}/../build/staging"
    CACHE PATH "webOS staging tree")

enable_testing()

set(SYSMGR "${CMAKE_CURRENT_SOURCE_DIR}/../components/luna-sysmgr")

# --- input plumbing, no webOS ------------------------------------------------
foreach (t touch-in eater-synthesis home-gesture)
    add_executable(${t} ${t}.cpp)
    target_link_libraries(${t} PRIVATE Qt::Core Qt::Gui Qt::Widgets Qt::GuiPrivate)
    add_test(NAME ${t} COMMAND ${t} -platform offscreen)
endforeach ()

# --- which coordinate space a touch point arrives in --------------------------
add_executable(touch-coordinate-spaces touch-coordinate-spaces.cpp)
target_link_libraries(touch-coordinate-spaces PRIVATE Qt::Core Qt::Gui Qt::Widgets Qt::GuiPrivate)
add_test(NAME touch-coordinate-spaces
         COMMAND touch-coordinate-spaces -platform offscreen)

# --- mouse to touch: a drag has to look like a drag -------------------------
add_executable(mouse-to-touch
    mouse-to-touch.cpp
    ${SYSMGR}/Src/base/MouseToTouch.cpp
)
target_include_directories(mouse-to-touch PRIVATE ${SYSMGR}/Src/base)
target_link_libraries(mouse-to-touch PRIVATE Qt::Core Qt::Gui Qt::GuiPrivate)
add_test(NAME mouse-to-touch COMMAND mouse-to-touch -platform offscreen)

# --- every .qml file must load under QtQuick 2 -------------------------------
add_executable(qml-loads qml-loads.cpp)
target_link_libraries(qml-loads PRIVATE Qt::Core Qt::Gui Qt::Qml Qt::Quick)
add_test(NAME qml-loads-luna-sysmgr
         COMMAND qml-loads -platform offscreen "${SYSMGR}/uiComponents")

# --- undefined identifiers inside QML functions -------------------------------
# Only the files that declare functions are worth calling; the rest are pure
# declarative markup and qml-loads already covers them.
add_executable(qml-functions qml-functions.cpp)
target_link_libraries(qml-functions PRIVATE Qt::Core Qt::Gui Qt::Qml Qt::Quick)
file(GLOB_RECURSE QML_WITH_FUNCTIONS "${SYSMGR}/uiComponents/*.qml")
add_test(NAME qml-functions
         COMMAND qml-functions -platform offscreen ${QML_WITH_FUNCTIONS})

# --- QmlSceneItem's contract -------------------------------------------------
# Compiles the adapter straight out of the shell's tree: it is the thing under
# test, and a copy here would drift.
add_executable(qml-scene-item
    qml-scene-item.cpp
    ${SYSMGR}/Src/base/QmlSceneItem.cpp
)
# QmlSceneItem.cpp opens with the shell's Common.h, which luna-sysmgr-common
# installs into staging.
target_include_directories(qml-scene-item PRIVATE
    ${SYSMGR}/Src ${SYSMGR}/Src/base ${GLIB_INCLUDE_DIRS}
    "${WEBOS_STAGING}/include/luna-sysmgr-common")
target_link_libraries(qml-scene-item PRIVATE
    Qt::Core Qt::Gui Qt::Widgets Qt::Qml Qt::Quick Qt::GuiPrivate Qt::Test ${GLIB_LIBRARIES})
add_test(NAME qml-scene-item COMMAND qml-scene-item -platform offscreen)

# --- what WebAppMgr needs from a web engine, and the layer built on it --------
find_package(Qt6 REQUIRED COMPONENTS WebEngineWidgets)

add_executable(webengine-capabilities webengine-capabilities.cpp)
target_link_libraries(webengine-capabilities PRIVATE
    Qt::Core Qt::Gui Qt::Widgets Qt::WebEngineWidgets Qt::Test)
add_test(NAME webengine-capabilities COMMAND webengine-capabilities)
# Runs without a display: QtWebEngine renders and takes input on the offscreen
# platform too.
set_tests_properties(webengine-capabilities PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# QtWebKit's API as WebAppMgr uses it, on QtWebEngine.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../components/qtwebkit-compat"
                 "${CMAKE_CURRENT_BINARY_DIR}/qtwebkit-compat-lib")
add_executable(qtwebkit-compat qtwebkit-compat.cpp)
target_link_libraries(qtwebkit-compat PRIVATE qtwebkit-compat-lib)
# As WebAppMgr is built; see the test for why it matters.
target_compile_options(qtwebkit-compat PRIVATE -fno-rtti)
add_test(NAME qtwebkit-compat COMMAND qtwebkit-compat)
set_tests_properties(qtwebkit-compat PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The WebView methods the mail app calls on a plain view. Without them its
# rendered() threw on setRedirects and the card never switched views.
add_executable(app-view-shims app-view-shims.cpp)
target_link_libraries(app-view-shims PRIVATE qtwebkit-compat-lib)
add_test(NAME app-view-shims COMMAND app-view-shims)
set_tests_properties(app-view-shims PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The prefixed transition events enyo listens for. Without them a Pane's
# transition never ends and the outgoing view is never hidden: two views paint
# at once, which looked like transparency in the mail card.
add_executable(prefixed-transition-event prefixed-transition-event.cpp)
target_link_libraries(prefixed-transition-event PRIVATE qtwebkit-compat-lib)
add_test(NAME prefixed-transition-event COMMAND prefixed-transition-event)
set_tests_properties(prefixed-transition-event PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# What an iframe can see. A QWebEngineScript is main-frame-only unless it says
# otherwise, so nothing of the bridge reached the mail card's three frames and
# the account wizard threw "PalmServiceBridge is not defined".
add_executable(subframe-bridge subframe-bridge.cpp)
target_link_libraries(subframe-bridge PRIVATE qtwebkit-compat-lib)
add_test(NAME subframe-bridge COMMAND subframe-bridge)
set_tests_properties(subframe-bridge PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Whether a signal reaches the frame that connected it. The reply goes out
# through QWebEnginePage::runJavaScript, which runs in the main frame alone, so
# the account wizard's iframe never got its listAccountTemplates callback and
# AccountWizard threw on a protValidators that was never assigned.
add_executable(subframe-signal subframe-signal.cpp)
target_link_libraries(subframe-signal PRIVATE qtwebkit-compat-lib)
add_test(NAME subframe-signal COMMAND subframe-signal)
set_tests_properties(subframe-signal PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# A second web view painted inside the card, where the browser app's dead
# <object type="application/x-palm-browser"> sits. Chromium has no plugin
# socket, so the replacement blits an embedded page's own pixels through the
# render() every app already goes through.
add_executable(embedded-view embedded-view.cpp)
target_link_libraries(embedded-view PRIVATE qtwebkit-compat-lib)
add_test(NAME embedded-view COMMAND embedded-view)
set_tests_properties(embedded-view PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Whether dragging inside the hole scrolls the page painted there -- that it
# scrolls at all, not how far. Nothing handles a wheel anywhere in the stack,
# and a mouse drag reads as a text selection, so the browser could be selected
# but not scrolled.
add_executable(embedded-scroll embedded-scroll.cpp)
target_link_libraries(embedded-scroll PRIVATE qtwebkit-compat-lib)
add_test(NAME embedded-scroll COMMAND embedded-scroll)
set_tests_properties(embedded-scroll PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Whether a click inside the hole reaches the page painted there. Without it
# the browser renders and nothing in it can be clicked or scrolled: every touch
# lands on the page that holds the hole.
add_executable(embedded-input embedded-input.cpp)
target_link_libraries(embedded-input PRIVATE qtwebkit-compat-lib)
add_test(NAME embedded-input COMMAND embedded-input)
set_tests_properties(embedded-input PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# What one repaint of a card costs, for deciding how to back the browser's
# <object> with a real view. Deliberately NOT add_test: it measures and always
# succeeds, and a test that cannot fail is noise in the suite. Run it by hand:
#   QT_QPA_PLATFORM=offscreen ./build/tests/grab-cost 1024 768
add_executable(grab-cost grab-cost.cpp)
target_link_libraries(grab-cost PRIVATE qtwebkit-compat-lib)

# What a window opened by window.open() can see in its first script. This one
# passes unmodified, and is kept for what it rules out: the child-window path
# was never the fault -- the frames the mail card makes for itself were.
add_executable(child-window-bridge child-window-bridge.cpp)
target_link_libraries(child-window-bridge PRIVATE qtwebkit-compat-lib)
add_test(NAME child-window-bridge COMMAND child-window-bridge)
set_tests_properties(child-window-bridge PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The prefixed frame canceller enyo pairs with webkitRequestAnimationFrame.
# Without it HP's "webkitCancelRequestAnimationFrame || window.clearTimeout"
# picks clearTimeout, and every cancelled frame clears the timeout that happens
# to carry the same number -- which is what froze the mail card's fade.
add_executable(frame-cancel frame-cancel.cpp)
target_link_libraries(frame-cancel PRIVATE qtwebkit-compat-lib)
add_test(NAME frame-cancel COMMAND frame-cancel)
set_tests_properties(frame-cancel PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The border box -webkit-border-image used to imply. Without it the calculator
# measured its keys at 121x94 instead of 91x64 and sized its font from that.
add_executable(border-image-box border-image-box.cpp)
target_link_libraries(border-image-box PRIVATE qtwebkit-compat-lib)
add_test(NAME border-image-box COMMAND border-image-box)
set_tests_properties(border-image-box PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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
# pbnjson installs into usr/ under staging. These looked only in lib/, found
# nothing, and pbnjson-smoke was skipped on every run without anyone noticing.
find_library(PBNJSON_C NAMES pbnjson_c
             PATHS "${WEBOS_STAGING}/lib" "${WEBOS_STAGING}/usr/lib" NO_DEFAULT_PATH)
find_library(PBNJSON_CPP NAMES pbnjson_cpp
             PATHS "${WEBOS_STAGING}/lib" "${WEBOS_STAGING}/usr/lib" NO_DEFAULT_PATH)
find_path(PBNJSON_INCLUDE pbnjson.h
          PATHS "${WEBOS_STAGING}/include" "${WEBOS_STAGING}/usr/include" NO_DEFAULT_PATH)
if (PBNJSON_C AND PBNJSON_INCLUDE)
    add_executable(pbnjson-smoke pbnjson-smoke.c)
    target_include_directories(pbnjson-smoke PRIVATE "${PBNJSON_INCLUDE}")
    target_link_libraries(pbnjson-smoke PRIVATE ${PBNJSON_C})
    add_test(NAME pbnjson-smoke COMMAND pbnjson-smoke)

    # JValue from a std::string: an assertion that assumed copy-on-write strings
    # aborted LunaSysMgr as soon as the virtual keyboard was enabled.
    if (PBNJSON_CPP)
        add_executable(pbnjson-cxx-string pbnjson-cxx-string.cpp)
        target_include_directories(pbnjson-cxx-string PRIVATE "${PBNJSON_INCLUDE}")
        target_link_libraries(pbnjson-cxx-string PRIVATE ${PBNJSON_CPP} ${PBNJSON_C})
        add_test(NAME pbnjson-cxx-string COMMAND pbnjson-cxx-string)
        # libpbnjson_cpp needs libpbnjson_c, and the executable's RUNPATH does
        # not apply to a library's own dependencies.
        get_filename_component(PBNJSON_LIBDIR "${PBNJSON_CPP}" DIRECTORY)
        set_tests_properties(pbnjson-cxx-string PROPERTIES
            ENVIRONMENT "LD_LIBRARY_PATH=${PBNJSON_LIBDIR}")
    endif ()
else ()
    message(WARNING "pbnjson not found under ${WEBOS_STAGING}; its tests are not built")
endif ()
