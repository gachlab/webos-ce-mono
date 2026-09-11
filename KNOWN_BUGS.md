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

### ~~Calendar and email open empty~~ (the JavaScript services run now)

A webOS JavaScript service boots on node 26 and answers on the bus:

    $ luna-send -n 1 palm://com.palm.location/getCurrentPosition '{}'
    {"latitude":37.390196,"longitude":-122.037845,"returnValue":true, ...}

That is HP's `GetCurrentPositionCommandAssistant.js`, loaded by mojoloader over
the foundations and mojoservice frameworks, through `run-js-service` and
`bootstrap-node.js` -- all of it HP's, none of it changed -- on top of
`components/node-v8-shim`. Checked by `tests/node-shim/service.sh`, which needs
the bus and the static services up, so it is run by hand rather than by ctest.

What it took, beyond the three addons:

- `Context` on node's own `vm` module rather than emulated. V8 gave each library
  its own global; foundations needs both halves of that, and neither shortcut
  works -- a function wrapper per script makes top-level vars local ("DB is not
  defined"), and sharing the interpreter's global rebinds `exports` under the
  closures ("exports.Comms is undefined"). `vm.createContext` is the real thing,
  reachable from an addon through `process.mainModule.require('vm')`.

- Seeding each context. A fresh vm context has none of node's globals, and
  foundations decides whether it is on node by looking for `root.process.version`
  -- without it, it takes the Mojo branch and calls `palmGetResource`, which only
  ever existed in HP's build.

- A `--require` shim for what HP's patched node had: `process.setName`,
  `process.setArgs`, `palmGetResource`, `palmPutResource`, `getenv`, `quit`, the
  `sys` module alias, and `new Buffer`. Ten call sites across 32,000 lines of
  JavaScript, which is why this is a shim and not a rewrite.

- Tests excluded from the installed frameworks and services. They ship mock
  versions of their own modules and call MojoLoader methods that exist only in a
  test harness.

**Still open.** The hub does not start these on demand: ls-hubd runs outside the
bwrap namespace on purpose, so `/usr/palm/services` is not there for it to
launch. They start the way HP's own static services do. Calendar and email
additionally need their own back ends, which is a separate question from whether
node runs.

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
