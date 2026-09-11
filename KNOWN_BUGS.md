# Known bugs

What is broken, what it depends on, and what is already ruled out. Each entry
says how it was diagnosed so nobody has to redo the work.

Verified against the reference where possible: an Ubuntu 12.04 VM running HP's
own `build-webos-desktop.sh`. **If it fails there too, it is not our port.**

---

## Blocked on a missing dependency

### The QML parts of the UI do not draw

**Symptom.** Notifications appear as an empty band with no text. The log only
says `QQmlComponent: Component is not ready`, which explains nothing.

**Cause.** Two things, in order:

1. `qml-module-qtquick2` is not installed. `QQmlComponent` cannot resolve
   `import QtQuick` and fails with that unhelpful message. Found by loading the
   file with a minimal standalone loader, which does print the real error:
   `module "QtQuick" is not installed`.
2. All 34 QML files use `import Qt 4.7`, which is QML1. Qt5 removed
   QtDeclarative. The elements they use (`Text`, `Image`, `Rectangle`,
   `ListView`, `MouseArea`, `Behavior`, `anchors`) all exist in QtQuick 2, so
   the port looks mechanical, but this has not been verified past (1).

**Affects more than notifications:** `QmlAlertWindow`, `SystemMenu`,
`LockWindow` and `dimensionslauncher` all load QML.

**Ruled out.** There is no shim: `qml-module-qtquick2` is the engine
(`libqtquick2plugin.so`), not an API surface. The `QtQuick1` compatibility
module that could load QML1 was removed in Qt 5.6. HP's own non-QML path
(`m_isOverlay == false`) covers only the dashboard, and we are already on it.

**Next step.** Install `qml-module-qtquick2`, then re-check (2).

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

### LunaUniversalSearchMgr dies inside the namespace

Four of HP's five static services run. This one exits immediately when started
inside the bwrap namespace and survives outside it, so it is a path the
namespace does not provide. Not yet traced. It is the Just Type search backend.

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
