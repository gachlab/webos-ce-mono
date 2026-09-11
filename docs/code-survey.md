# Code survey: what is actually being ported

Measured against HP's original drop, not estimated. Where a claim has since been
settled by doing the work, it says so.

## What this really is

**Not an operating system. A userland application stack.**

On the desktop target webOS ships no kernel, no drivers, no init, no
distribution. `LunaSysMgr` loads **no kernel modules of its own**, and its
system dependencies are ordinary:

```
c dl m pthread rt stdc++ gcc_s     C/C++ runtime
QtCore QtGui QtDeclarative
QtNetwork QtOpenGL QtScript QtSql  Qt 4  <- the real anchor
X11 xcb Xext Xfixes Xdamage GL drm X11 and graphics
sqlite3 yajl pcre z                ordinary libraries
```

Ubuntu 12.04 was **where it used to build**, not something the system drags in.
It now builds and runs on Debian sid with gcc 16 and Qt 5.15.

## Size

The monorepo is ~3.6M lines, but that is misleading: `build-support-ce` (1.4M)
is **headers**, and `nodejs` (297K) has V8 inside.

| Component | Lines | Note |
|---|---|---|
| `luna-sysmgr-ce` | **193K** | 153K `.cpp` + 39K `.h`, across 605 files. The heart |
| `mojomail`, `db8` | 89K + 66K | C++ services |
| `app-services`, `enyo-1.0`, `core-apps` | ~270K | JavaScript |
| `luna-service2` | 29K | The bus |

`luna-sysmgr-ce` is 293 `.cpp` files with a **median of 238 lines**. The largest
20 are **37%** of the code.

## Hardware coupling: none

```
inline ARM assembly .........  0 files
calls into nyx (the HW layer)  0 files
TARGET_DEVICE .............. 47 of 605 files (8%)
MACHINE_* (specific model) .. 10 files
```

HP isolated the hardware abstraction in `nyx-lib`, a separate library
LunaSysMgr **does not touch directly**.

## The real anchor was Qt 4

The classic blockers **are not there**:

```
Q_WS_*  (removed in Qt5) ....  0 files
QRegExp (removed in Qt6) ....  0 files
QHttp   .....................  0 files
```

What it uses heavily is still alive in Qt6: `QGraphicsView`/`QGraphicsScene`
(140 files), `QWidget` (135), `QPainter` (206).

**The one hard blocker is QtDeclarative (QML1)**, removed in Qt5: 19 files,
14,774 lines. Three carry half of it — `dimensionslauncher.cpp` (4,236),
`LockWindow.cpp` (2,838), `WindowServerLuna.cpp` (1,334). Plus 8 files with
`QGLWidget` -> `QOpenGLWidget`.

**Modernisation surface: 27 files**, not 605.

*Since confirmed:* the C++ side needed ~180 lines of changes and now builds and
runs on Qt 5.15. The QML1 blocker is real and still open — the 34 `.qml` files
all say `import Qt 4.7`. See `KNOWN_BUGS.md`.

## The web engine is isolated

`LunaSysMgr` **does not include a single WebKit header**. No `QtWebKit`, no
`QWebView`, no `QWebPage`. It talks to the engine over **IPC, in 65 files**; the
browser runs in a separate process (`BrowserServer`).

Consequence: the engine can be replaced without touching the window manager. And
since apps are web pages (`"type": "web"`, `main: index.html`), **modernising the
engine enables modern-JS apps without touching LunaSysMgr**.

Enyo 1.0 is 2011-era ES5 (verified: 0 arrow functions, 0 `let`, 0 `class`), but
it does not tie anything down: any framework works as long as it produces
`index.html` + `appinfo.json`.

## The device target is NOT buildable

`device.pri` requires 10 libraries. **Six exist nowhere**, neither source nor
binary:

| Library | ARM blob | Source |
|---|---|---|
| `luna-prefs`, `PmLogLib` | no | **yes** |
| `rolegen`, `serviceinstall` | **yes** | yes |
| `hid`, `memchute`, `media-api`, `napp`, `hal`, `affinity` | no | **DOES NOT EXIST** |

They are precisely the ones that touch hardware. That is what "Community
**Edition**" meant: HP released what it legally could.

This is why LunaCE built only `LunaSysMgr` and installed it onto a TouchPad
**already running webOS 3.0.5**: the blobs came from the device, not the release.

**The vanilla path to devices exists, but it is the other one**: in Open webOS,
HP rewrote those closed pieces as open (`nyx-lib` instead of the proprietary
`hal`) and left `build-webos` (OpenEmbedded) to build images. Still HP code from
2012.

## C++: plain pre-C++11

0 `nullptr`, 2 smart pointers across 605 files, raw pointers everywhere.

*Since confirmed:* the volume feared here did not materialise. Building with
gcc 16 took roughly 180 lines of changes across the whole tree, and nearly all
of them were APIs that moved underneath (`extern __inline` meaning the opposite
under C99, OpenSSL structs going opaque, `<cstdint>` no longer arriving
transitively) rather than anything wrong in HP's code.

## Plan, by increasing difficulty

| | Effort | Unblocks | State |
|---|---|---|---|
| Build system -> CMake | Low | Building offline, in parallel | **done** |
| Qt4 -> Qt5 | Medium: 27 files | Running on modern distros | **done for C++**, QML open |
| Web engine | Medium, **isolated by IPC** | Modern-JS apps | **done** (QtWebKit 5.212) |
| C++03 -> modern | High by volume | Current compilers | **done**, ~180 lines |

## What is NOT touched

The card model, the compositor on `QGraphicsView`, the luna bus, the process
separation and the app format. **That is webOS.** LuneOS changed exactly those
things and ended up a different system.

It is the difference between **restoring** and **rebuilding**.

## nodejs: it does not need building

HP's `nodejs` is **0.4.12** (2011) with V8 inside, built by **SCons written in
Python 2**. Porting that is a project with an uncertain tail.

It is not necessary. HP's patches to node (`#if WEBOS` in `node.cc`) are
**memory limits for an embedded device**:

```c
#define MAX_OLD_SPACE_OPTION  "--max_old_space_size 10485760"   // 10 MB
static int max_stack_size = 524288;                             // 512 KB
#if WEBOS
  abort();          // instead of exit(1)
```

None of it is webOS functionality: they are tweaks for a TouchPad with 1 GB of
RAM.

**The cheap path is the distribution's node** (Debian ships v26) and porting
only what is actually webOS:

| Piece | Size | What it is |
|---|---|---|
| `sysbus` | 2,660 lines | The bridge between JS and the luna bus. **The real work** |
| `dynaload` | 334 lines | Dynamic loading |
| `pmlog` | 104 lines | Logging |

They need moving from node 0.4's raw V8 API to N-API.

And the services' JavaScript (**17,671 lines**) is nearly clean:

```
require('sys')   3 uses   ->  called 'util' since node 0.8
new Buffer(      2 uses   ->  Buffer.from()
require('webos') 15 uses  ->  the native addon
```

Five fixes. Building V8 instead means fighting a Python 2 build system to end up
with a 2011 JavaScript engine.
