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
foreach (t touch-in eater-synthesis home-gesture wheel-in filter-order)
    add_executable(${t} ${t}.cpp)
    target_link_libraries(${t} PRIVATE Qt::Core Qt::Gui Qt::Widgets Qt::GuiPrivate)
    add_test(NAME ${t} COMMAND ${t} -platform offscreen)
endforeach ()

# --- the wheel crossing HP's IPC, packed by adapters/input-compat ----------
# No Qt at all: what is under test is the agreement between the two processes
# about which of SysMgrEvent's fields carry a scroll, and that the type chosen
# out of HP's reserved range trips none of his own event tests.
add_executable(wheel-pack wheel-pack.cpp)
target_include_directories(wheel-pack PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../adapters/input-compat/include"
    "${WEBOS_STAGING}/usr/include/sysmgr-ipc")
add_test(NAME wheel-pack COMMAND wheel-pack)

# The other event input-compat carries, and that the two never read as each
# other: both ride in HP's reserved User range off the same union.
add_executable(hover-pack hover-pack.cpp)
target_include_directories(hover-pack PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../adapters/input-compat/include"
    "${WEBOS_STAGING}/usr/include/sysmgr-ipc")
add_test(NAME hover-pack COMMAND hover-pack)

# --- which webOS key codes have to become Qt ones, and which must not --------
# The back gesture posts SysMgrDeviceKeydefs' Key_Escape (0x1B), which is not
# Qt's (0x01000000), so Chromium hands the DOM keyCode 0 and enyo's Gesture.js
# -- which tests keyCode == 27 and nothing else -- never synthesises "back".
# Measured in the running browser before the fix. Most of the test is about
# refusing to translate anything else: this runs on every key a card receives.
add_executable(key-translate key-translate.cpp)
target_include_directories(key-translate PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../adapters/input-compat/include")
target_link_libraries(key-translate PRIVATE Qt::Core)
add_test(NAME key-translate COMMAND key-translate)

# --- and where that key goes once it is the right one ------------------------
# Getting the code right was only half of it: measured in the running browser,
# the escape then landed in the embedded content, which ignores it, while enyo's
# listener sits in the app's own document. Keys follow the last press so that
# typing reaches a field inside the content; a gesture has to go to the app
# instead. Both directions are pinned here.
add_executable(gesture-key-routing gesture-key-routing.cpp)
target_link_libraries(gesture-key-routing PRIVATE qtwebkit-compat-lib)
add_test(NAME gesture-key-routing COMMAND gesture-key-routing)
set_tests_properties(gesture-key-routing PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- where the gesture strip's vertical drag begins, and where HP's ends -----
# Only the header is compiled, not the filter: the decision is a pure function
# precisely so it can be checked without bringing a shell up. What matters is
# the boundary -- claiming a sideways drag would fire Launcher on top of HP's
# own Back and the strip would do two things at once.
add_executable(strip-drag strip-drag.cpp)
target_include_directories(strip-drag PRIVATE "${SYSMGR}/Src/base")
target_link_libraries(strip-drag PRIVATE Qt::Core)
add_test(NAME strip-drag COMMAND strip-drag)

# --- what com.palm.power reports, from a fake /sys/class/power_supply ----------
# The status bar and DisplayManager each drop a payload missing a field they
# read, so a wrong name shows up as a battery icon that never moves rather than
# as a wrong number. The fields are checked as strings for that reason.
add_executable(power-state power-state.cpp)
target_include_directories(power-state PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../services/sysfs-powerd/src")
add_test(NAME power-state COMMAND power-state)

# --- what com.palm.connectionmanager reports, from a fake NetworkManager -------
# HP's stub answered "connected, over wifi, always". The mapping is header-only
# and free of both buses so it can be checked without a D-Bus daemon: what is
# under test is the payload the status bar, luna-sysservice, BrowserServer and
# activitymanager each read named fields out of -- including the one field whose
# absence segfaults the shell instead of showing a wrong icon.
add_executable(network-state network-state.cpp)
target_include_directories(network-state PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../services/nm-connectionmanager/src")
add_test(NAME network-state COMMAND network-state)

# --- what is read from NetworkManager, and what is asked of it ----------------
# The other half: nm_client.cpp against a fake NetworkManager on a private bus,
# started by GTestDBus -- so it needs dbus-daemon, and not the host's network.
pkg_check_modules(GIO REQUIRED gio-2.0)
set(CONNMGR "${CMAKE_CURRENT_SOURCE_DIR}/../services/nm-connectionmanager/src")
add_executable(nm-client nm-client.cpp ${CONNMGR}/nm_client.cpp ${CONNMGR}/sleep_watch.cpp)
target_include_directories(nm-client PRIVATE ${CONNMGR} ${GIO_INCLUDE_DIRS})
target_link_libraries(nm-client PRIVATE ${GIO_LIBRARIES})
add_test(NAME nm-client COMMAND nm-client)

# The certificate store the Wi-Fi card's enterprise TLS login lists.
add_executable(certificates certificates.cpp ${CONNMGR}/certificates.cpp)
target_include_directories(certificates PRIVATE ${CONNMGR} ${GIO_INCLUDE_DIRS})
target_link_libraries(certificates PRIVATE ${GIO_LIBRARIES})
add_test(NAME certificates COMMAND certificates)

# Per-network proxies for getNwProxiesConfig / configureNwProxies (#23).
add_executable(network-proxies network-proxies.cpp ${CONNMGR}/network_proxies.cpp)
target_include_directories(network-proxies PRIVATE
    ${CONNMGR} ${GIO_INCLUDE_DIRS}
    "${WEBOS_STAGING}/include" "${WEBOS_STAGING}/usr/include")
target_link_directories(network-proxies PRIVATE
    "${WEBOS_STAGING}/lib" "${WEBOS_STAGING}/usr/lib")
target_link_libraries(network-proxies PRIVATE ${GIO_LIBRARIES} cjson)
add_test(NAME network-proxies COMMAND network-proxies
         ENVIRONMENT "LD_LIBRARY_PATH=${WEBOS_STAGING}/lib:${WEBOS_STAGING}/usr/lib")

# --- which coordinate space a touch point arrives in --------------------------
add_executable(touch-coordinate-spaces touch-coordinate-spaces.cpp)
target_link_libraries(touch-coordinate-spaces PRIVATE Qt::Core Qt::Gui Qt::Widgets Qt::GuiPrivate)
add_test(NAME touch-coordinate-spaces
         COMMAND touch-coordinate-spaces -platform offscreen)

# --- whether the shell's window can be resized at all ------------------------
# Three things pinned it, and QLayout::SetFixedSize outranks the other two: with
# it in place, removing both setFixedSize calls changes nothing, so the fix
# looks done while the window still will not move. This holds that down, and
# checks a resize arrives carrying the size the display is computed from.
add_executable(window-resize window-resize.cpp)
target_link_libraries(window-resize PRIVATE Qt::Core Qt::Gui Qt::Widgets)
add_test(NAME window-resize COMMAND window-resize -platform offscreen)

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

# --- the cable's row in the system menu, tapped twice -------------------------
# Hosted in QmlSceneItem, as the shell hosts it, because the second bug was in
# the hosting.
add_executable(system-menu-wired
    system-menu-wired.cpp
    ${SYSMGR}/Src/base/QmlSceneItem.cpp
)
target_include_directories(system-menu-wired PRIVATE
    ${SYSMGR}/Src ${SYSMGR}/Src/base ${GLIB_INCLUDE_DIRS}
    "${WEBOS_STAGING}/include/luna-sysmgr-common")
target_link_libraries(system-menu-wired PRIVATE
    Qt::Core Qt::Gui Qt::Widgets Qt::Qml Qt::Quick Qt::GuiPrivate ${GLIB_LIBRARIES})
add_test(NAME system-menu-wired
         COMMAND system-menu-wired -platform offscreen
                 ${SYSMGR}/uiComponents/SystemMenu/SystemMenu.qml)

# --- which apps came with the system, under the rootfs prefix ------------------
add_executable(rom-app-path rom-app-path.cpp)
target_include_directories(rom-app-path PRIVATE ${SYSMGR}/Src/base/application)
add_test(NAME rom-app-path COMMAND rom-app-path)

# --- who answers to an app id, once a rewrite inherits the address it replaces -
add_executable(app-aliases app-aliases.cpp)
target_include_directories(app-aliases PRIVATE ${SYSMGR}/Src/base/application)
add_test(NAME app-aliases COMMAND app-aliases)

# --- what the system menu opens the Wi-Fi card with ---------------------------
add_executable(wifi-launch-params wifi-launch-params.cpp)
target_include_directories(wifi-launch-params PRIVATE ${SYSMGR}/Src/lunaui/status-bar)
target_link_libraries(wifi-launch-params PRIVATE Qt::Core)
add_test(NAME wifi-launch-params COMMAND wifi-launch-params)

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

# --- QmlGraphicsSlot: a native item where the QML keeps its place ------------
# The dashboard menu's notifications, which QtQuick 2 cannot parent.
add_executable(qml-graphics-slot
    qml-graphics-slot.cpp
    ${SYSMGR}/Src/base/QmlGraphicsSlot.cpp
    ${SYSMGR}/Src/base/QmlSceneItem.cpp
    ${SYSMGR}/Src/base/TouchAsMouse.cpp
)
target_include_directories(qml-graphics-slot PRIVATE
    ${SYSMGR}/Src ${SYSMGR}/Src/base ${GLIB_INCLUDE_DIRS}
    "${WEBOS_STAGING}/include/luna-sysmgr-common")
target_link_libraries(qml-graphics-slot PRIVATE
    Qt::Core Qt::Gui Qt::Widgets Qt::Qml Qt::Quick Qt::GuiPrivate Qt::Test ${GLIB_LIBRARIES})
add_test(NAME qml-graphics-slot COMMAND qml-graphics-slot -platform offscreen)

# --- the cards: the SDK, and the template card in the engine -----------------
# The SDK needs neither bus nor browser; the card runs the built bundle the
# way WebAppMgr loads it, against a fake PalmServiceBridge.
add_test(NAME web-sdk COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../tools/test-web.sh")
set_tests_properties(web-sdk PROPERTIES SKIP_RETURN_CODE 77)

# And the shape of the tree itself: components/ is HP's and nothing else.
add_test(NAME repo-layout COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/repo-layout.sh")
set_tests_properties(repo-layout PROPERTIES SKIP_RETURN_CODE 77)

add_test(NAME qtwebkit-compat-split COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/qtwebkit-compat-split.sh")
set_tests_properties(qtwebkit-compat-split PROPERTIES SKIP_RETURN_CODE 77)

add_executable(kit-elements kit-elements.cpp)
target_link_libraries(kit-elements PRIVATE qtwebkit-compat-lib)
add_test(NAME kit-elements COMMAND kit-elements)
set_tests_properties(kit-elements PROPERTIES
    SKIP_RETURN_CODE 77
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;WEBOS_CARDS_BUILD=${CMAKE_CURRENT_SOURCE_DIR}/../build/cards")

# A card that uses none of our renderer: the check behind #65's claim that the
# platform and the controls are usable from anything.
add_executable(plain-card plain-card.cpp)
target_link_libraries(plain-card PRIVATE qtwebkit-compat-lib)
add_test(NAME plain-card COMMAND plain-card)
set_tests_properties(plain-card PROPERTIES
    SKIP_RETURN_CODE 77
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;WEBOS_CARDS_BUILD=${CMAKE_CURRENT_SOURCE_DIR}/../build/cards")

add_executable(template-card template-card.cpp)
target_link_libraries(template-card PRIVATE qtwebkit-compat-lib)
add_test(NAME template-card COMMAND template-card)
set_tests_properties(template-card PROPERTIES
    SKIP_RETURN_CODE 77
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;WEBOS_CARDS_BUILD=${CMAKE_CURRENT_SOURCE_DIR}/../build/cards")

# --- storaged: what an erase erases, and what it must never touch ------------
set(STORAGED "${CMAKE_CURRENT_SOURCE_DIR}/../services/storaged")
add_executable(storaged-erase storaged-erase.cpp ${STORAGED}/src/erase.cpp)
target_include_directories(storaged-erase PRIVATE ${STORAGED}/src ${CJSON_INCLUDE_DIRS}
    "${WEBOS_STAGING}/include")
target_link_directories(storaged-erase PRIVATE "${WEBOS_STAGING}/lib" "${WEBOS_STAGING}/usr/lib")
target_link_libraries(storaged-erase PRIVATE cjson)
add_test(NAME storaged-erase COMMAND storaged-erase)
set_tests_properties(storaged-erase PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${WEBOS_STAGING}/lib:${WEBOS_STAGING}/usr/lib")

# A touch a container keeps reaches the window under the finger as mouse.
add_executable(touch-as-mouse touch-as-mouse.cpp ${SYSMGR}/Src/base/TouchAsMouse.cpp)
target_include_directories(touch-as-mouse PRIVATE ${SYSMGR}/Src/base)
target_link_libraries(touch-as-mouse PRIVATE Qt::Core Qt::Gui Qt::Widgets Qt::Test)
add_test(NAME touch-as-mouse COMMAND touch-as-mouse -platform offscreen)

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

# Same five capabilities against WPE WebKit + WPEPlatform headless (#81).
# Distro packages only — no engine build. Skip cleanly when -dev is absent.
pkg_check_modules(WPE_CAPABILITIES wpe-webkit-2.0 wpe-platform-headless-2.0)
if(WPE_CAPABILITIES_FOUND)
    add_executable(wpe-capabilities wpe-capabilities.cpp)
    set_target_properties(wpe-capabilities PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_include_directories(wpe-capabilities PRIVATE ${WPE_CAPABILITIES_INCLUDE_DIRS})
    target_compile_options(wpe-capabilities PRIVATE ${WPE_CAPABILITIES_CFLAGS_OTHER})
    target_link_libraries(wpe-capabilities PRIVATE ${WPE_CAPABILITIES_LIBRARIES})
    add_test(NAME wpe-capabilities COMMAND wpe-capabilities)
    # WPEProcess / headless display: one suite at a time (parallel runs race on first frame).
    set_tests_properties(wpe-capabilities PROPERTIES RESOURCE_LOCK wpe)

    # WPE frame blit into the ARGB32_Premultiplied surface WebAppMgr paints to.
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../adapters/wpe-webcontent"
                     "${CMAKE_CURRENT_BINARY_DIR}/wpe-webcontent-lib")
    add_executable(wpe-paint-path wpe-paint-path.cpp)
    set_target_properties(wpe-paint-path PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(wpe-paint-path PRIVATE wpe-webcontent-lib Qt6::Gui)
    add_test(NAME wpe-paint-path COMMAND wpe-paint-path)
    set_tests_properties(wpe-paint-path PROPERTIES RESOURCE_LOCK wpe)

    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../adapters/wpewebkit-compat"
                     "${CMAKE_CURRENT_BINARY_DIR}/wpewebkit-compat-lib")
    add_executable(wpewebkit-compat wpewebkit-compat.cpp)
    set_target_properties(wpewebkit-compat PROPERTIES AUTOMOC ON)
    target_compile_options(wpewebkit-compat PRIVATE -fno-rtti)
    target_link_libraries(wpewebkit-compat PRIVATE wpewebkit-compat-lib)
    add_test(NAME wpewebkit-compat COMMAND wpewebkit-compat)
    set_tests_properties(wpewebkit-compat PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        RESOURCE_LOCK wpe)

    # Same plain-card contract as qtwebkit-compat, on the WPE backend (#81).
    add_executable(wpe-plain-card plain-card.cpp)
    target_compile_options(wpe-plain-card PRIVATE -fno-rtti)
    target_link_libraries(wpe-plain-card PRIVATE wpewebkit-compat-lib)
    add_test(NAME wpe-plain-card COMMAND wpe-plain-card)
    set_tests_properties(wpe-plain-card PROPERTIES
        SKIP_RETURN_CODE 77
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;WEBOS_CARDS_BUILD=${CMAKE_CURRENT_SOURCE_DIR}/../build/cards"
        RESOURCE_LOCK wpe)
else()
    message(STATUS "wpe-capabilities / wpe-paint-path / engine-scroll-load / wpewebkit-compat / wpe-plain-card: skipped (install libwpewebkit-2.0-dev)")
endif()

# QtWebKit's API as WebAppMgr uses it, on QtWebEngine.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../adapters/qtwebkit-compat"
                 "${CMAKE_CURRENT_BINARY_DIR}/qtwebkit-compat-lib")
add_executable(qtwebkit-compat qtwebkit-compat.cpp)
target_link_libraries(qtwebkit-compat PRIVATE qtwebkit-compat-lib)
# As WebAppMgr is built; see the test for why it matters.
target_compile_options(qtwebkit-compat PRIVATE -fno-rtti)
add_test(NAME qtwebkit-compat COMMAND qtwebkit-compat)
set_tests_properties(qtwebkit-compat PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Under-load scroll with image-diff (#81); WPE vs qtwebkit-compat paint path.
if(TARGET wpe-webcontent-lib)
    add_executable(engine-scroll-load engine-scroll-load.cpp)
    set_target_properties(engine-scroll-load PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(engine-scroll-load PRIVATE
        wpe-webcontent-lib qtwebkit-compat-lib Qt6::Gui Qt6::Widgets)
    add_test(NAME engine-scroll-load COMMAND engine-scroll-load)
    set_tests_properties(engine-scroll-load PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 120
        RESOURCE_LOCK wpe)

    # Many-card load A/B (#81). Not a ctest default: 150 Chromium views is a
    # bench, run by hand (or CI job with a high timeout). Small n smoke below.
    add_executable(engine-heavy-stack engine-heavy-stack.cpp)
    set_target_properties(engine-heavy-stack PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(engine-heavy-stack PRIVATE
        wpe-webcontent-lib qtwebkit-compat-lib Qt6::Gui Qt6::Widgets)
    add_executable(engine-card-load engine-card-load.cpp)
    set_target_properties(engine-card-load PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(engine-card-load PRIVATE
        wpe-webcontent-lib qtwebkit-compat-lib Qt6::Gui Qt6::Widgets)
    add_test(NAME engine-card-load-smoke-wpe COMMAND engine-card-load wpe 3)
    add_test(NAME engine-card-load-smoke-qt COMMAND engine-card-load qtwebengine 3)
    set_tests_properties(engine-card-load-smoke-wpe engine-card-load-smoke-qt PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 120
        RESOURCE_LOCK wpe)
endif()

# What a dashboard window needs: its window.open attributes, a transparent
# render, and its clipped layers left clipped.
add_executable(dashboard-window dashboard-window.cpp)
target_link_libraries(dashboard-window PRIVATE qtwebkit-compat-lib)
add_test(NAME dashboard-window COMMAND dashboard-window)
set_tests_properties(dashboard-window PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# A file the engine would download goes to the page that asked, as the browser
# plugin's mimeNotSupported did, and the engine's own download is refused.
add_executable(download-request download-request.cpp)
target_link_libraries(download-request PRIVATE qtwebkit-compat-lib)
add_test(NAME download-request COMMAND download-request)
set_tests_properties(download-request PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# A page WebAppMgr focuses, or that is typed into, has the focus, and its
# fields hear about it.
add_executable(page-focus page-focus.cpp)
target_link_libraries(page-focus PRIVATE qtwebkit-compat-lib)
add_test(NAME page-focus COMMAND page-focus)
set_tests_properties(page-focus PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# An app's own document can call web services that send no CORS headers.
add_executable(remote-requests remote-requests.cpp)
target_link_libraries(remote-requests PRIVATE qtwebkit-compat-lib Qt::Network)
add_test(NAME remote-requests COMMAND remote-requests)
set_tests_properties(remote-requests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The browser's content area stops being painted while its app hides it.
add_executable(browser-view-hole browser-view-hole.cpp)
target_link_libraries(browser-view-hole PRIVATE qtwebkit-compat-lib)
add_test(NAME browser-view-hole COMMAND browser-view-hole)
set_tests_properties(browser-view-hole PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The browser's Preferences reach the engine through WebAppMgr's view adapter.
add_executable(browser-preferences browser-preferences.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../components/webappmanager/Src/webbase/BrowserViewAdapter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../components/webappmanager/Src/webbase/BrowserViewAdapter.h")
target_include_directories(browser-preferences PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../components/webappmanager/Src/webbase")
find_package(Qt6 REQUIRED COMPONENTS Network)
target_link_libraries(browser-preferences PRIVATE qtwebkit-compat-lib Qt::Network)
add_test(NAME browser-preferences COMMAND browser-preferences)
set_tests_properties(browser-preferences PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# A web page's navigator.geolocation goes through com.palm.location.
find_package(Qt6 REQUIRED COMPONENTS Positioning)
set(GEOLOCATION_ADAPTER "${CMAKE_CURRENT_SOURCE_DIR}/../components/webappmanager/Src/webbase/GeolocationAdapter.cpp")
add_executable(geolocation geolocation.cpp ${GEOLOCATION_ADAPTER}
    "${CMAKE_CURRENT_SOURCE_DIR}/../components/webappmanager/Src/webbase/GeolocationAdapter.h")
# Built into the executable, as in WebAppMgr.
set_source_files_properties(${GEOLOCATION_ADAPTER} PROPERTIES COMPILE_DEFINITIONS QT_STATICPLUGIN)
target_include_directories(geolocation PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../components/webappmanager/Src/webbase")
target_link_libraries(geolocation PRIVATE qtwebkit-compat-lib Qt::Network Qt::Positioning)
add_test(NAME geolocation COMMAND geolocation)
set_tests_properties(geolocation PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Per-network proxies become Qt's application proxy for WebAppMgr / the browser.
set(NETWORK_APP_PROXY_ADAPTER
    "${CMAKE_CURRENT_SOURCE_DIR}/../components/webappmanager/Src/webbase/NetworkAppProxyAdapter.cpp")
add_executable(network-app-proxy network-app-proxy.cpp ${NETWORK_APP_PROXY_ADAPTER})
target_include_directories(network-app-proxy PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../components/webappmanager/Src/webbase"
    "${CMAKE_CURRENT_SOURCE_DIR}/../services/nm-connectionmanager/src")
target_link_libraries(network-app-proxy PRIVATE Qt::Core Qt::Network)
add_test(NAME network-app-proxy COMMAND network-app-proxy)
set_tests_properties(network-app-proxy PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The WebView methods the mail app calls on a plain view. Without them its
# rendered() threw on setRedirects and the card never switched views.
add_executable(app-view-shims app-view-shims.cpp)
target_link_libraries(app-view-shims PRIVATE qtwebkit-compat-lib)
add_test(NAME app-view-shims COMMAND app-view-shims)
set_tests_properties(app-view-shims PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Number inputs that keep text, as the address fields of the Wi-Fi settings
# need; Chromium drops anything that is not a floating-point number.
add_executable(number-inputs number-inputs.cpp)
target_link_libraries(number-inputs PRIVATE qtwebkit-compat-lib)
add_test(NAME number-inputs COMMAND number-inputs)
set_tests_properties(number-inputs PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The Wi-Fi settings card, run as WebAppMgr runs it: the built bundle and the
# kit's elements, with com.palm.wifi stood in for. It used to load the enyo card
# and reach into enyo's own objects; the card is ours now (#38) and what is left
# to check from out here is the wiring -- the calls it sends, the replies it is
# pushed, and the screen each one leaves behind.
add_executable(wifi-card wifi-card.cpp)
target_link_libraries(wifi-card PRIVATE qtwebkit-compat-lib)
add_test(NAME wifi-card COMMAND wifi-card)
set_tests_properties(wifi-card PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;WEBOS_CARDS_BUILD=${CMAKE_CURRENT_SOURCE_DIR}/../build/cards"
    TIMEOUT 240)

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

# --- services in modern TypeScript --------------------------------------------
# services/node-services: type check, then the kit's tests, twice over for the
# bus (an in-memory palmbus and a real ls-hubd) and once for db8. The script
# gives the run a /tmp of its own, since the hub's sockets live there.
add_test(NAME node-services COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/../services/node-services/test/run.sh")
set_tests_properties(node-services PROPERTIES SKIP_RETURN_CODE 77)

# --- one webOS session at a time ---------------------------------------------
# A second start while one is up must leave it alone. Found live: the new
# supervisor's startup teardown killed the running session, and two starts
# settled on a shell with no bus. Runs against stubs; see the script for why the
# cases that let a start through skip themselves while a real session is up.
add_test(NAME session-lock COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/session-lock/run.sh")

# --- OpenSearch descriptions, parsed by the universal search manager ----------
# Its own sources, less main(), against the staged libraries.
set(USM "${CMAKE_CURRENT_SOURCE_DIR}/../components/luna-universalsearchmgr")
pkg_check_modules(USM_DEPS REQUIRED libxml-2.0 sqlite3 gthread-2.0)
add_executable(opensearch-parse opensearch-parse.cpp
    ${USM}/Src/OpenSearchHandler.cpp ${USM}/Src/SearchItemsManager.cpp ${USM}/Src/SearchServiceManager.cpp
    ${USM}/Src/UniversalSearchPrefsDb.cpp ${USM}/Src/UniversalSearchService.cpp ${USM}/Src/USUtils.cpp
    ${USM}/Src/Logging.cpp)
target_include_directories(opensearch-parse PRIVATE ${USM}/include/public ${GLIB_INCLUDE_DIRS} ${USM_DEPS_INCLUDE_DIRS}
    "${WEBOS_STAGING}/include" "${WEBOS_STAGING}/usr/include")
target_link_directories(opensearch-parse PRIVATE "${WEBOS_STAGING}/lib" "${WEBOS_STAGING}/usr/lib")
target_link_libraries(opensearch-parse PRIVATE luna-service2 cjson ${USM_DEPS_LIBRARIES} ${GLIB_LIBRARIES})
add_test(NAME opensearch-parse COMMAND opensearch-parse)
set_tests_properties(opensearch-parse PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${WEBOS_STAGING}/lib:${WEBOS_STAGING}/usr/lib")

# The provider list Just Type and the browser use: HTTPS, with suggestions.
add_test(NAME search-providers COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/search-providers.py")

# --- the bus leaves when it is told to ------------------------------------------
# ls-hubd's SIGTERM handler called g_main_loop_quit() inside the signal and could
# wait forever on the main loop's lock; it hung a CI run (#48).
add_test(NAME hub-shutdown COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/hub-shutdown/run.sh")
set_tests_properties(hub-shutdown PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)

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
