# Known bugs

What is broken, what it depends on, and what is already ruled out. Each entry
says how it was diagnosed so nobody has to redo the work.

Verified against the reference where possible: an Ubuntu 12.04 VM running HP's
own `build-webos-desktop.sh`. **If it fails there too, it is not our port.**

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

**Still open in this area.** `SystemMenu.qml:71,73` logs
`ReferenceError: inProgress is not defined`, and the 34 files in
`components/luna-sysmgr-ce/` still carry the QML 1 import.

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

**Worth knowing when checking any of this by hand:** luna-send's `-P` is the
PUBLIC bus and no flag is private. A com.palm service listens on the private bus,
and on the public one only if it has commands marked "public", so a non-public
method answering "is not running" on `-P` is by design. db8 answering -3963
"permission denied" to a find means the kind IS registered; -3970 means it is not.

**Still open.**

- `com.palm.tempdb`'s .service runs mojodb-luna on /var/db, the same directory as
  com.palm.db. Nothing starts tempdb today; if something does, the two will
  contend for the lock.
- run-js-service prints `Failure writing to tasks file "/no-group/not-present"`
  on every launch. That path is HP's own deliberate fallback for a device without
  its cgroup setup, commented as such in the script; it is harmless.
- The accounts service reports `Found 0 account templates`. Calendar and email
  still need account back ends before they show anything, which is a separate
  question from whether their services run.

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

### A line crosses the Just Type search field

Two thin horizontal lines are drawn inside the search input, one through the
text. It is an Enyo `RichText`; the CSS renders differently on QtWebKit 5.212
than on HP's WebKit. Cosmetic.

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
TouchPad's 3.0.5) still needs its Qt5 port; the build currently runs Open
webOS's `luna-sysmgr`.
