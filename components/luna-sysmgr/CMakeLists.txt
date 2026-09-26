# LunaSysMgr, translated from sysmgr.pro.
#
# HP's .pro is kept: it is the original, it documents the device and emulator
# targets, and its generated Makefile was the authority used to build this list.
# This covers the desktop target only -- its linux-g++ branch.
#
# 201 sources and 134 moc outputs. The source list was not derived by reading the
# .pro top to bottom (conditional blocks make that unreliable) but by taking the
# object list out of qmake's generated Makefile and resolving each name through
# VPATH. That is what actually got built.

cmake_minimum_required(VERSION 3.16)
project(LunaSysMgr CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(WEBOS_STAGING "${CMAKE_INSTALL_PREFIX}")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Widgets Quick Qml OpenGL Sql)
# Qt 6 moved QOpenGLWidget and its relatives out of Gui into OpenGLWidgets.
# Qt 6 also moved QStateMachine and its transitions into their own module
# (Debian: qt6-scxml-dev). MouseToTouch and QmlSceneItem fill in touch points
# through QMutableEventPoint, which is private API (GuiPrivate). The rest of
# what Qt 6 removed comes back through adapters/qt6-compat.
find_package(Qt6 REQUIRED COMPONENTS OpenGLWidgets StateMachine GuiPrivate)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../adapters/qt6-compat ${CMAKE_CURRENT_BINARY_DIR}/qt6-compat)
# Input modern hardware produces that webOS's catalogue never had -- the wheel,
# so far -- packed into HP's Event without changing it.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../adapters/input-compat ${CMAKE_CURRENT_BINARY_DIR}/input-compat)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../adapters/dmabuf-window ${CMAKE_CURRENT_BINARY_DIR}/dmabuf-window)
# Qt 6's moc needs the complete type behind every pointer-typed property.
# icon.h declares Q_PROPERTY(LayoutItem * ...) with LayoutItem only forward
# declared, which Qt 5 accepted. -b puts the definition in front of every
# generated moc file instead of editing HP's header.
list(APPEND CMAKE_AUTOMOC_MOC_OPTIONS -b
     "${CMAKE_CURRENT_SOURCE_DIR}/Src/lunaui/launcher/elements/page/icon_layouts/layoutitem.h")
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0 gthread-2.0)
pkg_check_modules(SQLITE REQUIRED sqlite3)
find_package(OpenGL REQUIRED)

add_executable(LunaSysMgr
    Src/Main.cpp
    Src/base/AmbientLightSensor.cpp
    Src/base/AppDirectRenderingArbitrator.cpp
    Src/base/BackupManager.cpp
    Src/base/CoreNaviLeds.cpp
    Src/base/CoreNaviManager.cpp
    Src/base/CpuAffinity.cpp
    Src/base/DisplayManager.cpp
    Src/base/DisplayStates.cpp
    Src/base/EASPolicyManager.cpp
    Src/base/EventReporter.cpp
    Src/base/EventThrottler.cpp
    Src/base/EventThrottlerIme.cpp
    Src/base/FpsHistory.cpp
    Src/base/HapticsController.cpp
    Src/base/HostWindow.cpp
    Src/base/InputManager.cpp
    Src/base/LsmUtils.cpp
    Src/base/MemoryMonitor.cpp
    Src/base/MetaKeyManager.cpp
    Src/base/ProgressAnimation.cpp
    Src/base/Security.cpp
    Src/base/SuspendBlocker.cpp
    Src/base/SystemService.cpp
    Src/base/SystemUiController.cpp
    Src/base/UiNavigationController.cpp
    Src/base/Window.cpp
    Src/base/WindowManagerBase.cpp
    Src/base/WindowServer.cpp
    Src/base/application/ApplicationDescription.cpp
    Src/base/application/ApplicationInstaller.cpp
    Src/base/application/ApplicationManager.cpp
    Src/base/application/ApplicationManagerService.cpp
    Src/base/application/ApplicationStatus.cpp
    Src/base/application/CmdResourceHandlers.cpp
    Src/base/application/LaunchPoint.cpp
    Src/base/application/MimeSystem.cpp
    Src/base/application/PackageDescription.cpp
    Src/base/application/ServiceDescription.cpp
    Src/base/gesture/SingleClickGestureRecognizer.cpp
    Src/base/settings/AnimationSettings.cpp
    Src/base/settings/DeviceInfo.cpp
    Src/base/settings/Preferences.cpp
    Src/base/visual/CardDropShadowEffect.cpp
    Src/base/visual/ReticleItem.cpp
    Src/base/visual/RoundedCorners.cpp
    Src/base/visual/TouchPlot.cpp
    Src/base/visual/TouchToShareGlow.cpp
    Src/base/visual/WSOverlayScreenShotAnimation.cpp
    Src/base/windowdata/HostWindowData.cpp
    Src/base/windowdata/HostWindowDataDmaBuf.cpp
    Src/base/windowdata/HostWindowDataOpenGL.cpp
    Src/base/windowdata/HostWindowDataSoftware.cpp
    Src/core/KeywordMap.cpp
    Src/core/MallocHooks.cpp
    Src/ime/IMEController.cpp
    Src/ime/IMEManager.cpp
    Src/ime/IMEView.cpp
    Src/ime/InputClient.cpp
    Src/ime/InputWindowManager.cpp
    Src/ime/KeyLocationRecorder.cpp
    Src/ime/SysmgrIMEDataInterface.cpp
    Src/ime/VirtualKeyboardPreferences.cpp
    Src/lunaui/FullEraseConfirmationWindow.cpp
    Src/lunaui/GraphicsItemContainer.cpp
    Src/lunaui/PersistentWindowCache.cpp
    Src/lunaui/WindowServerLuna.cpp
    Src/lunaui/cards/CardGroup.cpp
    Src/lunaui/cards/CardHostWindow.cpp
    Src/lunaui/cards/CardLoading.cpp
    Src/lunaui/cards/CardWindow.cpp
    Src/lunaui/cards/CardWindowManager.cpp
    Src/lunaui/cards/CardWindowManagerStates.cpp
    Src/lunaui/cards/GhostCard.cpp
    Src/lunaui/cards/QtHostWindow.cpp
    Src/lunaui/dock/DockModeAppMenuContainer.cpp
    Src/lunaui/dock/DockModeClock.cpp
    Src/lunaui/dock/DockModeLaunchPoint.cpp
    Src/lunaui/dock/DockModeMenuManager.cpp
    Src/lunaui/dock/DockModePositionManager.cpp
    Src/lunaui/dock/DockModeWindow.cpp
    Src/lunaui/dock/DockModeWindowManager.cpp
    Src/lunaui/emergency/EmergencyWindowManager.cpp
    Src/lunaui/launcher/OverlayWindowManager.cpp
    Src/lunaui/launcher/QuicklaunchLayout.cpp
    Src/lunaui/launcher/debugglobal.cpp
    Src/lunaui/launcher/dimensionsglobal.cpp
    Src/lunaui/launcher/dimensionslauncher.cpp
    Src/lunaui/launcher/dimensionsmain.cpp
    Src/lunaui/launcher/dynamicssettings.cpp
    Src/lunaui/launcher/elements/bars/pagetab.cpp
    Src/lunaui/launcher/elements/bars/pagetabbar.cpp
    Src/lunaui/launcher/elements/bars/quicklaunchbar.cpp
    Src/lunaui/launcher/elements/buttons/colorroundrectbutton.cpp
    Src/lunaui/launcher/elements/buttons/labeledbutton.cpp
    Src/lunaui/launcher/elements/buttons/pixbutton.cpp
    Src/lunaui/launcher/elements/buttons/pixbutton2state.cpp
    Src/lunaui/launcher/elements/buttons/pixbuttonsimple.cpp
    Src/lunaui/launcher/elements/icons/icon.cpp
    Src/lunaui/launcher/elements/icons/icondecorator.cpp
    Src/lunaui/launcher/elements/icons/icongeometrysettings.cpp
    Src/lunaui/launcher/elements/icons/iconheap.cpp
    Src/lunaui/launcher/elements/page/alphabetpage.cpp
    Src/lunaui/launcher/elements/page/icon_layouts/alphabeticonlayout.cpp
    Src/lunaui/launcher/elements/page/icon_layouts/iconlayout.cpp
    Src/lunaui/launcher/elements/page/icon_layouts/iconlayoutsettings.cpp
    Src/lunaui/launcher/elements/page/icon_layouts/iconreorderanimation.cpp
    Src/lunaui/launcher/elements/page/icon_layouts/layoutitem.cpp
    Src/lunaui/launcher/elements/page/icon_layouts/reorderableiconlayout.cpp
    Src/lunaui/launcher/elements/page/page.cpp
    Src/lunaui/launcher/elements/page/pagemovement.cpp
    Src/lunaui/launcher/elements/page/reorderablepage.cpp
    Src/lunaui/launcher/elements/static/horizontaldivider.cpp
    Src/lunaui/launcher/elements/static/horizontallabeleddivider.cpp
    Src/lunaui/launcher/elements/static/overlaylayer.cpp
    Src/lunaui/launcher/elements/static/picturebox.cpp
    Src/lunaui/launcher/elements/static/staticelementsettings.cpp
    Src/lunaui/launcher/elements/static/textbox.cpp
    Src/lunaui/launcher/elements/util/KineticScroller.cpp
    Src/lunaui/launcher/elements/util/dotgrid.cpp
    Src/lunaui/launcher/elements/util/stringtranslator.cpp
    Src/lunaui/launcher/gfx/debug/pixpagerdebugger.cpp
    Src/lunaui/launcher/gfx/debug/qtjsonabstract.cpp
    Src/lunaui/launcher/gfx/debug/sysmgrdebuggerservice.cpp
    Src/lunaui/launcher/gfx/effects/gfxeffectbase.cpp
    Src/lunaui/launcher/gfx/effects/gfxsepiaeffect.cpp
    Src/lunaui/launcher/gfx/gfxsettings.cpp
    Src/lunaui/launcher/gfx/pixmaploader.cpp
    Src/lunaui/launcher/gfx/pixmapobject/pixmap3htileobject.cpp
    Src/lunaui/launcher/gfx/pixmapobject/pixmap3vtileobject.cpp
    Src/lunaui/launcher/gfx/pixmapobject/pixmap9tileobject.cpp
    Src/lunaui/launcher/gfx/pixmapobject/pixmapfilmstripobject.cpp
    Src/lunaui/launcher/gfx/pixmapobject/pixmaphugeobject.cpp
    Src/lunaui/launcher/gfx/pixmapobject/pixmapjupocobject.cpp
    Src/lunaui/launcher/gfx/pixmapobject/pixmapjupocrefobject.cpp
    Src/lunaui/launcher/gfx/pixmapobject/pixmapobject.cpp
    Src/lunaui/launcher/gfx/pixpager.cpp
    Src/lunaui/launcher/gfx/processors/expblur.cpp
    Src/lunaui/launcher/gfx/renderedlabel.cpp
    Src/lunaui/launcher/gfx/scrollableobject.cpp
    Src/lunaui/launcher/gfx/scrollinglayoutrenderer.cpp
    Src/lunaui/launcher/gfx/scrollingsurface.cpp
    Src/lunaui/launcher/gfx/vcamera.cpp
    Src/lunaui/launcher/groupanchoritem.cpp
    Src/lunaui/launcher/iconcmdevents.cpp
    Src/lunaui/launcher/layoutsettings.cpp
    Src/lunaui/launcher/operationalsettings.cpp
    Src/lunaui/launcher/physics/motion/frictiontransform.cpp
    Src/lunaui/launcher/physics/motion/linearmotiontransform.cpp
    Src/lunaui/launcher/systeminterface/appeffector.cpp
    Src/lunaui/launcher/systeminterface/appmonitor.cpp
    Src/lunaui/launcher/systeminterface/externalapp.cpp
    Src/lunaui/launcher/systeminterface/pagerestore.cpp
    Src/lunaui/launcher/systeminterface/pagesaver.cpp
    Src/lunaui/launcher/systeminterface/util/blacklist.cpp
    Src/lunaui/launcher/systeminterface/util/filenames.cpp
    Src/lunaui/launcher/systeminterface/util/safefileops.cpp
    Src/lunaui/launcher/systeminterface/util/staticmatchlist.cpp
    Src/lunaui/launcher/systeminterface/webosapp.cpp
    Src/lunaui/launcher/test/testiconfactory.cpp
    Src/lunaui/launcher/thing.cpp
    Src/lunaui/launcher/thingpaintable.cpp
    Src/lunaui/launcher/util/conditionalsignaltransition.cpp
    Src/lunaui/launcher/util/propertysettingsignaltransition.cpp
    Src/lunaui/launcher/util/timedelaytransition.cpp
    Src/lunaui/launcher/util/variableanimsignaltransition.cpp
    Src/lunaui/lockscreen/ClockWindow.cpp
    Src/lunaui/lockscreen/LockWindow.cpp
    Src/lunaui/lockscreen/TopLevelWindowManager.cpp
    Src/lunaui/notifications/ActiveCallBanner.cpp
    Src/lunaui/notifications/AlertWindow.cpp
    Src/lunaui/notifications/BannerMessageEventFactory.cpp
    Src/lunaui/notifications/BannerMessageHandler.cpp
    Src/lunaui/notifications/BannerWindow.cpp
    Src/lunaui/notifications/DashboardWindow.cpp
    Src/lunaui/notifications/DashboardWindowContainer.cpp
    Src/lunaui/notifications/DashboardWindowManager.cpp
    Src/lunaui/notifications/DashboardWindowManagerStates.cpp
    Src/lunaui/notifications/NativeAlertManager.cpp
    Src/lunaui/notifications/NotificationPolicy.cpp
    Src/lunaui/notifications/QmlAlertWindow.cpp
    Src/lunaui/notifications/VolumeControlAlertWindow.cpp
    Src/lunaui/status-bar/BtDeviceClass.cpp
    Src/lunaui/status-bar/MenuWindow.cpp
    Src/lunaui/status-bar/MenuWindowManager.cpp
    Src/lunaui/status-bar/StatusBar.cpp
    Src/lunaui/status-bar/StatusBarBattery.cpp
    Src/lunaui/status-bar/StatusBarClock.cpp
    Src/lunaui/status-bar/StatusBarIcon.cpp
    Src/lunaui/status-bar/StatusBarInfo.cpp
    Src/lunaui/status-bar/StatusBarItemGroup.cpp
    Src/lunaui/status-bar/StatusBarNotificationArea.cpp
    Src/lunaui/status-bar/StatusBarServicesConnector.cpp
    Src/lunaui/status-bar/StatusBarTitle.cpp
    Src/lunaui/status-bar/SystemMenu.cpp
    Src/minimalui/WindowManagerMinimal.cpp
    Src/minimalui/WindowServerMinimal.cpp
    Src/remote/IpcClientHost.cpp
    Src/remote/IpcServer.cpp
    Src/remote/WebAppMgrProxy.cpp
    Src/sound/SoundPlayerDummy.cpp
    Src/sound/SoundPlayerPool.cpp
    Src/base/QmlSceneItem.cpp
    Src/base/QmlGraphicsSlot.cpp
    Src/base/TouchAsMouse.cpp
    Src/base/MouseToTouch.cpp
    Src/base/WheelToScroll.cpp
    Src/base/HoverToMouseMove.cpp
    Src/base/StripDragToCoreNavi.cpp

    # Q_OBJECT headers AUTOMOC would not find on its own. qmake ran moc over
    # everything in HEADERS; AUTOMOC pairs X.cpp with an X.h in the SAME
    # directory. These have no .cpp, or theirs lives elsewhere, so their
    # meta-objects would go missing silently -- the build succeeds and the link
    # fails with qt_metacall undefined.
    Src/base/MouseEventEater.h
    Src/lunaui/launcher/OverlayWindowManager_p.h
    Src/base/QmlInputItemQt5.h
    Src/base/gesture/SingleClickGesture.h
    Src/lunaui/status-bar/StatusBarItem.h

    # The ime headers live in staging, not in this tree. sysmgr.pro puts them in
    # HEADERS for exactly this reason: nothing else provides IMEDataInterface's
    # meta-object or the IMEData_* template ones.
    ${WEBOS_STAGING}/include/ime/IMEData.h
    ${WEBOS_STAGING}/include/ime/IMEDataInterface.h
)

# qmake's VPATH, which doubles as INCLUDEPATH ("INCLUDEPATH = $$VPATH").
target_include_directories(LunaSysMgr PRIVATE
    Src
    Src/base
    Src/base/application
    Src/base/gesture
    Src/base/settings
    Src/base/visual
    Src/base/windowdata
    Src/core
    Src/ime
    Src/lunaui
    Src/lunaui/cards
    Src/lunaui/dock
    Src/lunaui/emergency
    Src/lunaui/launcher
    Src/lunaui/launcher/elements
    Src/lunaui/launcher/elements/bars
    Src/lunaui/launcher/elements/buttons
    Src/lunaui/launcher/elements/icons
    Src/lunaui/launcher/elements/page
    Src/lunaui/launcher/elements/page/icon_layouts
    Src/lunaui/launcher/elements/static
    Src/lunaui/launcher/elements/util
    Src/lunaui/launcher/gfx
    Src/lunaui/launcher/gfx/debug
    Src/lunaui/launcher/gfx/effects
    Src/lunaui/launcher/gfx/pixmapobject
    Src/lunaui/launcher/gfx/processors
    Src/lunaui/launcher/physics
    Src/lunaui/launcher/physics/motion
    Src/lunaui/launcher/systeminterface
    Src/lunaui/launcher/systeminterface/util
    Src/lunaui/launcher/test
    Src/lunaui/launcher/util
    Src/lunaui/lockscreen
    Src/lunaui/notifications
    Src/lunaui/status-bar
    Src/minimalui
    Src/remote
    Src/sound
    ${WEBOS_STAGING}/include
    ${WEBOS_STAGING}/include/cjson
    ${WEBOS_STAGING}/include/luna-service2
    ${WEBOS_STAGING}/include/luna-sysmgr-common
    ${WEBOS_STAGING}/include/ime
    ${WEBOS_STAGING}/usr/include
    ${WEBOS_STAGING}/usr/include/sysmgr-ipc
    ${GLIB_INCLUDE_DIRS}
)

target_compile_definitions(LunaSysMgr PRIVATE
    QT_WEBOS
    # HP's own device.pri and emulator.pri both define this; the CMake build
    # never did, so every getLunaPrefSystemValue() call compiled to "return
    # false". That is not cosmetic: DeviceInfo decides m_wifiAvailable from
    # whether com.palm.properties.WIFIoADDR resolves, and with it false the
    # status bar never subscribes to com.palm.wifi and SystemMenu hides the wifi
    # entry -- so the wifi indicator could not move whatever answered the bus.
    #
    # The other five sites it switches on are bounded and degrade by design:
    # AmbientLightSensor only gains an include, and DisplayManager gains a
    # demo-mode check on buildName and a read of its saved brightness, each
    # guarded by LP_ERR_NONE.
    HAS_LUNA_PREF=1
    SHIPPING_VERSION=0
    TARGET_DESKTOP
    HAVE_OPENGL
    HAVE_DMABUF
    P_BACKEND=P_BACKEND_SOFT
    QT_NO_KEYWORDS          # CONFIG += no_keywords: webOS uses "signal" as a name
    FIX_FOR_QT
)

target_compile_options(LunaSysMgr PRIVATE
    -fno-rtti -fno-exceptions
    -fvisibility=hidden -fvisibility-inlines-hidden
    -Wall -fpermissive
    -Wno-unused-parameter -Wno-unused-variable -Wno-reorder
    -Wno-missing-field-initializers -Wno-extra
)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# Taken from the LIBS line of qmake's generated Makefile rather than from the
# .pro: conditional blocks add to it, and reading the .pro top to bottom misses
# nyx (CoreNaviLeds.cpp calls it directly) and the OpenSSL pair.
target_link_libraries(LunaSysMgr PRIVATE
    Qt::Core Qt::Gui Qt::Network Qt::Widgets
    Qt::Quick Qt::Qml Qt::OpenGL Qt::Sql
    Qt::OpenGLWidgets Qt::StateMachine Qt::GuiPrivate qt6compat-gl input-compat dmabuf-window
    ${GLIB_LDFLAGS} ${SQLITE_LDFLAGS} OpenGL::GL
    OpenSSL::SSL OpenSSL::Crypto Threads::Threads
    -L${WEBOS_STAGING}/lib -L${WEBOS_STAGING}/usr/lib
    cjson LunaSysMgrIpc luna-service2 pbnjson_cpp pbnjson_c
    LunaSysMgrCommon nyx luna-prefs
)

# No INSTALL_RPATH here any more. It used to name ${WEBOS_STAGING} absolutely,
# which baked one developer's home directory into the shipped binary and made
# the tree impossible to relocate. tools/build.sh now passes
# CMAKE_INSTALL_RPATH=$ORIGIN/.. for every component at once, so the rootfs
# works from wherever it is unpacked. A per-target property would override that.

install(TARGETS LunaSysMgr RUNTIME DESTINATION bin)
