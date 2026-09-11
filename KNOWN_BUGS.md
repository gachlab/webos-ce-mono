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

### Calendar and email open empty

**Cause.** They depend on JS services (`com.palm.service.accounts`,
`com.palm.service.calendar`) that run on node, and the node addons
(`sysbus`, `pmlog`, `dynaload`) use the old v8 API and have not been ported to
N-API. No node process is running.

**Not our port.** The VM fails with the exact same messages:
`Accounts.getAccounts: 0 accounts and 0 templates` and
`CalendarsManager.getCalendarsFailed(): getCalendars call failed`.

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

### Just Type's touch coordinates go through a fudge factor

**Not fixed. Found by sweeping HP's Qt 5 desktop branch, not by a symptom.**

`OverlayWindowManager::mapCoordToWindow` converts a position from the manager's
coordinates into the window's. Under Qt 5 on the desktop it does not:

    #if defined TARGET_DESKTOP && (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
        y -= kTouchPointYOffset;          // 50. x is not converted at all.
    #else
        QPointF pt = win->mapFromItem(this, x, y);
        QRectF br = win->boundingRect();
        x = pt.x() - br.x();
        y = pt.y() - br.y();
    #endif

That is downstream of the cause: `handleTouchBegin/End/Update` read
`scenePos()`, where the mouse twin a few lines below reads `pos()`. With the
wrong space going in, the real conversion could not work, and a constant was
subtracted until it roughly lined up.

**Why it is not a one-line fix.** Those handlers are fed from two delivery paths
at once -- `OverlayWindowManager::sceneEvent` and
`WindowServerLuna::sysmgrEventFilters`, the latter only while universal search is
open -- and the same accessor means different things in each
(`tests/touch-coordinate-spaces-qt5.cpp` pins down which). Swapping the accessor
fixes one path and breaks the other. It wants an explicit position argument, with
each caller converting in its own space, and a way to exercise Just Type to check
the result.

Guarded by `tools/check-touch-vs-mouse.py`, which lists it as known.

### tapAndHoldGesture is still rejected

Ordinary taps work since mouse events are translated into touch events in
`WindowServer::deliverAsTouch`. Long-press on the quick launch bar still logs
`Rejected; touch FSM reports no touchId is currently tracked`, so the FSM is not
fully fed for that gesture. Used for reordering icons.

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
