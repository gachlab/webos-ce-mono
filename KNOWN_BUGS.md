# Known bugs

What is broken, what it depends on, and what is already ruled out. Each entry
says how it was diagnosed so nobody has to redo the work.

Verified against the reference where possible: an Ubuntu 12.04 VM running HP's
own `build-webos-desktop.sh`. **If it fails there too, it is not our port.**

---

## The Qt 6 port

The build is Qt 6.10 with QtWebEngine, in `build/`: `tools/build.sh`, then
`tools/run-lunasysmgr.sh`. There is no Qt 5 build any more and nothing compiles
QtWebKit 5.212. The shell draws, WebAppMgr starts its QtWebEngine processes, and
the apps run.

How it was done: build against Qt 6, inventory every error with `make -k`, and
fix each where it can be fixed without touching HP's code. The Qt 5 build stayed
alongside until the apps ran on Qt 6, and was then dropped.

- **WebAppMgr's QtWebKit** (`components/qtwebkit-compat`): the QtWebKit classes it
  uses, over QtWebEngine, without changing WebAppMgr's web code. Pages render
  offscreen and are grabbed; input goes to the view's focus proxy; objects added
  with `addToJavaScriptWindowObject` become JavaScript proxies whose properties
  and methods answer synchronously through a synchronous XHR to a
  `webos-bridge:///` scheme answered in-process, and whose signals arrive through
  `runJavaScript`. `tests/webengine-capabilities` checks each QtWebEngine
  capability this relies on, and `tests/qtwebkit-compat` drives the layer the way
  SysMgrWebBridge does.
- **Adapters** (`components/qt6-compat`, only compiled for Qt 6): the `QGL*`
  classes over `QOpenGL*`, and a forced include that brings back `qrand`,
  `qSort`, `qFind`, `qVariantFromValue` and `qRegisterMetaTypeStreamOperators`
  and includes `<QObject>`, which Qt 5's headers pulled in for HP's.
- **Module moves**, in CMake: `QStateMachine` (Debian: `qt6-scxml-dev`),
  `QOpenGLWidget`, and `GuiPrivate` for `QMutableEventPoint`.
- **Members Qt removed from its own classes** cannot be adapted from outside, so
  about 85 call sites changed, each to a form that compiles on Qt 5.15 too:
  `horizontalAdvance`, `sizeInBytes`, `QMultiMap::insert`, `QChar(int(key))`,
  `drawRoundedRect(..., Qt::RelativeSize)`, and similar.

Traps found on the way, each confirmed before being fixed:

- **`QQuickWindow::graphicsApi()` reports OpenGL after `setGraphicsApi(Software)`.**
  A probe printed 3 from the static call while the window's
  `rendererInterface()` said 1 and rendered the right pixel. Deciding on the
  static value made `QmlSceneItem` call `QQuickRenderControl::initialize()`,
  which with the software adaptation creates an unusable RHI ("QRhi is only
  compatible with default adaptation") and every later `sync()`/`render()`
  refuses. Qt 6 documents that `initialize()` must not be called there.
- **The software renderer repaints only dirty regions.** Clearing the target
  image before each render left it empty on any frame with nothing dirty. Both
  mistakes were put back one at a time; `tests/qml-scene-item` fails on each.
- **A touch sent with `sendEvent()` never reaches QML under Qt 6.** QQuickWindow
  only accepts points its device tracks as active, and a device only tracks
  points that came in through the platform: "point is not in activePoints".
  Real touches in the shell do come through the platform; the test now injects
  through `QTest::touchEvent` on Qt 6.
- **`Window`'s copy constructor was declared and never defined**, with
  `HostWindow` a friend. Qt 6's moc instantiates the copy constructor of every
  QObject subclass that looks copyable, so the link failed; it is `= delete` now.
- **Qt 6's moc needs the complete type behind a pointer property**
  (`Q_PROPERTY(LayoutItem * ...)` in icon.h). CMake passes `-b layoutitem.h` to
  moc instead of editing the header.

- **WebAppMgr segfaulted in `__dynamic_cast` on its first `loadProgress`.** It is
  built with `-fno-rtti` (HP's flag), so `SysMgrWebPage` has no type_info, and in
  a Debug build Qt 6 dynamic_casts the receiver of a signal connected to a
  member function. The layer connects through lambdas. `tests/qtwebkit-compat`
  now subclasses QWebPage and builds with `-fno-rtti` and in Debug, and with the
  member-function connections back it segfaults the same way; the tests had been
  built without a build type, where Qt's checks do not exist, and passed.
- **LunaSysMgrCommon calls sqlite3 without linking it.** QtWebKit used to bring
  libsqlite3 into every executable, so nobody noticed until WebAppMgr stopped
  linking QtWebKit. It is linked by the library now.
- **A custom scheme is reachable from `file://` pages only when flagged
  `LocalScheme` and `SecureScheme` and addressed without a host
  (`webos-bridge:///...`).** Without either the request never reaches the handler.

- **A first-ever start needs `init` AFTER `services`, and the tools now enforce
  it.** configurator registers every db8 kind by calling com.palm.db, so db8 has
  to be answering first. Run before the services stage, it rejected all 35 kinds
  with "com.palm.db is not running" and left the database empty -- and nothing
  said so out loud: the shell drew, the apps started, and they were simply empty.
  Memos opened with nothing and refused to add; calendar retried getCalendars
  every ten seconds with "kind not registered"; email logged the same for its own
  kinds. The old build trees hid it, their database already holding the kinds
  from earlier sessions. `tools/run-lunasysmgr.sh init` now starts mojodb-luna if
  it is not running and waits for com.palm.db before configuring: 41 kinds and 66
  permissions, 0 failed, and the apps fill up.

- **What a clean start leaves in the logs, so none of it gets investigated
  twice.** With the kinds loaded first, email walks its whole startup: carrier
  defaults, account list, `com.palm.app.email.prefs:1` created and loaded, the
  folder and email change processors watching, the card opening, and MailApp
  reaching its first-launch view with the one account template we install.
  Calendar reaches first launch too. What is left is noise with a known cause:
  * `enyo.xhr.request() exception ... tellurium_config.json` -- enyo catches this
    itself (`try { send() } catch` in enyo-build.js) and returns undefined.
    Tellurium is left out on purpose, and a device without it logs the same line.
  * `_CallAcquire failed` -- a `g_debug()` in luna-service2's own client
    (callmap.c:1090) when a reply arrives for a call already off the map.
  * `Service does not exist: com.palm.power` / `com.palm.audio` / `com.palm.vpn`
    -- components this tree does not build.
  * `calendar.AppIcon.updateIconFailed` -- the app-icon update service is absent.
  The single real failure left is `this.$.body.setRedirects is not a function`
  (MessageDisplay.js:906, HtmlView.js:99), which is enyo's WebView control
  forwarding to the browser adapter. Email needs the browser to render a message
  body and for nothing else: the rest of the app runs without one.

Not done yet:

- **Checked by hand on Qt 6:** the shell and the apps run, and the line QtWebKit
  5.212 drew across Enyo text fields is gone. Not yet gone through app by app.
- **`/usr/palm/frameworks/tellurium` is not in the rootfs,** so every enyo app logs
  a failed load of `tellurium_config.json`. Not Qt 6's doing: the file exists in
  `components/enyo-1.0` and assemble-rootfs.sh never installs that directory.
- `QGLWidget` over `QOpenGLWidget` differs in one way that can matter at
  runtime: Qt 5 created the GL context in the constructor, Qt 6 only when the
  widget is first shown, so `makeCurrent()` right after construction does nothing.

---

## Blocked on a missing dependency

### ~~The QML parts of the UI do not draw~~ (fixed)

**Symptom.** Notifications appeared as an empty band with no text. The log only
said `QQmlComponent: Component is not ready`, which explains nothing.

**Cause.** Three things, in order:

1. `qml-module-qtquick2` was not installed, so `QQmlComponent` could not resolve
   the import. Found by loading the file with a minimal standalone loader
   (`tools/qml-check.cpp`), which does print the real error.
2. All 34 QML files opened with `import Qt 4.7`, which is QML 1; Qt 5 removed
   QtDeclarative. Measured with that loader: 0/34 loaded, 30/34 after swapping
   the import for `QtQuick 2.0`. The other 4 use types registered from C++ and
   cannot be checked standalone.
3. Even loading, none of it drew. A QtQuick 2 root is a `QQuickItem`, which is
   not a `QGraphicsItem`, so the `qobject_cast<QGraphicsObject*>` at all seven
   call sites returned null -- silently, and every site guards on null.

**Fix.** `QmlSceneItem` (`Src/base/QmlSceneItem.h`) hosts a QtQuick 2 scene
inside the QGraphicsScene: an offscreen `QQuickWindow` driven by
`QQuickRenderControl`, grabbed to a `QImage` and blitted in `paint()`, with
scene events translated back into window events. Callers keep the QML root
through `rootItem()`.

**Worth knowing if this breaks again.**

- Input arrives as touch, not mouse: `WindowServer::deliverAsTouch` converts it
  and `MouseEventEater` swallows the rest. The host must
  `setAcceptTouchEvents(true)` and forward touch.
- `QQuickRenderControl::initialize()` warns and returns without initializing
  unless handed the context that is already current. The shell draws through a
  `QGLWidget`, so one usually is.

`SystemMenu.qml`'s `ReferenceError: inProgress is not defined` was a typo for
`airplaneModeInProgress`, fixed along with `MenuContainer.qml`'s, and
`tests/qml-functions-qt5` catches both. The 34 files in
`components/luna-sysmgr-ce/` still carry the QML 1 import; that tree is kept for
reference and not built.

### ~~Calendar and email open empty~~ (the JavaScript services run, on demand)

All six of HP's JavaScript services start when something calls them and answer,
with LunaSysMgr and WebAppMgr up and nothing started by hand:

    com.palm.location                    answered
    com.palm.connectionmanager           answered
    com.palm.service.accounts            answered
    com.palm.service.contacts            answered
    com.palm.service.contacts.linker     answered
    com.palm.service.calendar.reminders  answered

That is `tests/node-shim/service.sh`, which needs the bus and the static services
up and so is run by hand. Underneath: HP's JavaScript unchanged, on node 26,
through `components/node-v8-shim`.

What had to be true for that, each of which was broken:

- **The hub starts services through the namespace.** ls-hubd stays outside the
  bwrap namespace on purpose -- it identifies callers through /proc/<pid>/exe --
  so anything it launched straight from the rootfs saw the host's filesystem.
  Every .service file now goes through `run-lunasysmgr.sh js-service` (for
  JavaScript, via bash: run-js-service uses `==` in `[ ]`, which dash rejects) or
  `run-lunasysmgr.sh ns-exec` (for C++).
- **Every service's bus files are installed.** Four of the six were skipped
  outright: the install read an `id` from services.json, and the app-services do
  not have one.
- **db8 keeps its data.** mojodb-luna wrote into the tmpfs over /var, so every
  restart of the services emptied the database. /var/db is now bound from the
  rootfs; the kinds survive a restart with no init in between, checked by asking
  db8 for them.
- **init loads the kinds.** When configurator started by init had not registered
  in time, the hub launched a copy from outside the namespace, which found
  nothing in /etc/palm/db/kinds -- 2 configurations instead of 41. With ns-exec
  that copy sees the same paths.
- **luna-send has a role.** HP's templates in luna-service2 were never installed,
  so the private hub refused luna-send outright.
- **Two of HP's JSON files parse.** The accounts services.json and the palmprofile
  account template had trailing commas.
- **There are account templates.** The accounts service lists every template
  under /usr/palm/public/accounts, and HP's -- in mojomail and
  app-services/account-templates -- were never installed, so it said `Found 0
  account templates` and there was no kind of account to add. It now lists four:
  com.palm.othermail, com.palm.palmprofile (contacts, calendar, tasks, memos,
  phone, messaging, file storage), com.palm.imap and com.palm.pop.
- **Reassembling with the bus up leaves the hub consistent.** ls-hubd reloads
  whenever its .conf is written (inotify on the conf directory), and
  assemble-rootfs.sh used to write it early and fix its paths midway, so a
  running hub reloaded halfway through and kept a view with the JavaScript
  services missing ("Service not listed in service files", every file correct on
  disk). The .conf is now written last, only when it changes, and the running
  hubs get one SIGHUP at the end. Checked by reassembling twice with everything
  up: one reload per hub each time, all six services answering, no manual step.

**Not a problem, though it looks like one:** `com.palm.tempdb`'s .service runs
mojodb-luna on /var/db, the same directory as com.palm.db. HP's desktop config
does too, and it is fine: one mojodb-luna process opens both services
(MojDbLunaServiceApp.cpp opens MainDir and TempDir under the same directory), so
the hub never has a second one to start. com.palm.tempdb answers with the single
process that is already running.

**Worth knowing when checking any of this by hand:** luna-send's `-P` is the
PUBLIC bus and no flag is private. A com.palm service listens on the private bus,
and on the public one only if it has commands marked "public", so a non-public
method answering "is not running" on `-P` is by design. db8 answering -3963
"permission denied" to a find means the kind IS registered; -3970 means it is not.

**Still open.**

- run-js-service prints `Failure writing to tasks file "/no-group/not-present"`
  on every launch. That path is HP's own deliberate fallback for a device without
  its cgroup setup, commented as such in the script; it is harmless.
- Calendar and email have account types to add now, but no account has been
  added and synced end to end. That needs the mail transports (mojomail-imap,
  -pop, -smtp) running against a real server, a separate question from whether
  their services run.

---

## Open, cause not yet found

### ~~LunaUniversalSearchMgr dies inside the namespace~~ (never did)

It was reported dead in every status line while up to thirteen copies of it were
running at once -- one leaked per launch, all session.

Linux truncates a task's comm to 15 characters. `pgrep -x` and `pkill -x` match
against comm, so for `LunaUniversalSearchMgr`, which is 22, they match nothing.
The status line read that as dead, and the same truncation stopped the script's
`pkill -x` from ever reaping the previous copy. pgrep does say so, on stderr:

    pgrep: pattern that searches for process name longer than 15 characters
           will result in zero matches

run-lunasysmgr.sh now compares argv[0] out of /proc, which is exact and has no
length limit. All five of HP's static services report alive.

Guarded by tests/long-process-name.sh.

### Just Type's touch coordinates went through a fudge factor (fixed, unverified)

`OverlayWindowManager::mapCoordToWindow` converts a position from the manager's
coordinates into the window's. Under Qt 5 on the desktop it did not:

    y -= kTouchPointYOffset;          // 50. x was not converted at all.

That was downstream of the cause. Its touch handlers read `scenePos()` where the
mouse twin reads `pos()`, and the events reaching them come from
`WindowServerLuna::sysmgrEventFilters` -- the raw viewport event, before
QGraphicsView translated anything -- so nothing was in the space the conversion
needed and a constant was subtracted until it roughly lined up.

The `sceneEvent` branch that would have delivered them the other way is
unreachable: neither the class nor `WindowManagerBase` ever calls
`setAcceptTouchEvents`. That is what made the fix tractable -- there is only one
live path, so the position can be converted once, on the way in, and the real
conversion restored.

**Not verified by using it.** It builds, the shell runs, and
`tools/check-touch-vs-mouse.py` no longer reports the mismatch, but driving Just
Type with synthetic input did not work well enough to confirm taps land where
they should. Worth trying by hand: open Just Type, tap in the field, and see
whether the caret goes where you tapped.

### ~~A line crosses the Just Type search field~~ (gone on Qt 6)

Two thin horizontal lines are drawn inside the search input, one through the
text. It is an Enyo `RichText`; the CSS renders differently on QtWebKit 5.212
than on HP's WebKit. Cosmetic.

QtWebEngine does not draw them, so they are gone (checked by hand). They were
only ever a QtWebKit 5.212 rendering difference.

---

## Known and accepted

### db8 builds without leveldb

HP pinned leveldb 1.9; Debian does not package it. db8 configures, builds and
answers queries without it. Whether `mojodb-luna` needs that backend for
anything beyond what we exercise is still unverified.

### The bus logs errors that come from HP

Both appear identically in the reference VM:

- `Attempting to add duplicate service name to permission map:
  "com.palm.lunastats"` — two role files declare the same name.
- `Unable to get permission from JSON (.../com.palm.nodejs.json)` — that file
  has a `role` section and no `permissions`, which the parser requires.

### `(null)` clients rejected on the bus

Anonymous clients with no registered name. They are `luna-send` invocations and
calls LunaSysMgr makes during startup, before it acquires its bus name. Not
symptomatic.

---

## Not built

`BrowserServer`, `BrowserAdapter` and `WebKitSupplemental` are the browser path
and depend on real NPAPI, not just an include path. `luna-sysmgr-ce` (the
TouchPad's 3.0.5) is not built on purpose: Open webOS's `luna-sysmgr` is the
reference implementation, and the CE one stays in the tree for reference only.
