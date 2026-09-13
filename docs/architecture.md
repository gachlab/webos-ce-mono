# webOS architecture

![webOS architecture](webos-architecture-palm.png)

A slide from a **Palm** presentation, contemporary with this code. Kept as study
material: it is the map you can place every component of the monorepo against.

## How it maps onto this repository

| Box in the diagram | Components | State on modern Debian |
|---|---|---|
| **Palm Bus** (the orange column) | `luna-service2` | builds |
| **OS Services / OS Middleware** | `pmloglib`, `nyx-lib`, `libsandbox`, `jemalloc`, `filecache`, `db8`, `configurator`, `luna-prefs`, `luna-init`, `librolegen`, `pmstatemachineengine` | build |
| **App Services** | `luna-universalsearchmgr`, `mojomail`, `activitymanager` | build; the JS ones need node |
| **UI System Manager** | `luna-sysmgr-ipc`, `luna-sysmgr-ipc-messages` and **`LunaSysMgr`** | builds and runs |
| **Browser / DocViewers** (over NPAPI) | `BrowserServer`, `BrowserAdapter`, WebKit | replaced, not ported: see below |
| **Media / Wireless** | — | HP never released them |
| Everything below *Kernel/User Space Boundary* | — | out of scope: no kernel, no drivers |

## Three things the diagram makes clear

**The bus is the spine, literally.** It is drawn as the vertical bar everything
else plugs into. It survived intact all the way to LG's webOS.

**"UI System Manager" is four boxes in a single process.** Window Manager,
Window Server, Mojo Framework and Application Manager all live inside
`LunaSysMgr`, and they line up with its largest files:
`ApplicationManagerService.cpp` (4,536 lines), `WindowServerLuna.cpp`,
`WindowServer.cpp`.

**NPAPI is the boundary to the browser.** The diagram puts Browser and
DocViewers OUTSIDE the UI System Manager. That is why `LunaSysMgr` does not
include a single WebKit header and talks to the engine over IPC:
`BrowserAdapter` is, literally, an NPAPI plugin. The practical consequence is
that the web engine can be replaced without touching the window manager.

That is not a hypothetical here: it is what was done. Chromium has no plugin
socket, so `BrowserServer` and `BrowserAdapter` (~29k lines between them) are
not ported at all. In their place, `QWebPage::embedPage` paints one page inside
another — the browser app's dead `<object type="application/x-palm-browser">`
becomes a hole that a second web view is blitted into — and `BrowserViewAdapter`
answers the app's `goBack`, `reload`, `setUrl` and the rest straight to
QtWebEngine. The window manager never learned about any of it, exactly as the
diagram promises.

## Adapters are the architecture, not a convenience

The rule this port is built on, and the number that measures it: **6,740 lines
of new code beside HP's against 1,804 inside it**. When something does not work
on modern Linux, the first question is which adapter it belongs in — not which
of HP's files to open.

Three adapters carry most of it (`qt6-compat`, `qtwebkit-compat`,
`node-v8-shim`), and the shell has a fourth shape for the same idea: an object
of ours, in its own file, reached from one guarded hook. `MouseEventEater` and
`MouseToTouch` are exactly that — port code living in `luna-sysmgr/Src/base`,
ours by copyright, installed from a `#if defined TARGET_DESKTOP` block in
`Main.cpp`.

So when a new behaviour is needed, in order of preference:

1. **An adapter.** It maps an interface HP's code already calls onto what the
   modern library offers. Nothing of HP's changes.
2. **An object of ours plus one install hook.** For behaviour that has to sit in
   the event stream rather than behind an interface. The hook is one block; the
   behaviour is in our file.
3. **HP's own extension points**, where they exist. `Event::Type` reserves
   `User = 0xFF000000` for exactly this, and reusing a carried-but-unused field
   beats widening a struct that `ParamTraits` sends as raw bytes.
4. **Editing HP's code.** Last resort. It has to be justified in the commit
   message, and when it is a whole class of change, in the "What cannot be
   adapted from outside" section of `qt6-compat/README.md`.

Why this matters beyond tidiness: every line inside HP's files is a line that
has to be re-reasoned if the drop is ever re-imported or diffed against the
original, and it is the difference between *webOS running on modern Linux* and
*a fork of webOS*. The claim in the README is only true for as long as the
ratio is.

**And where the gaps are:** the *Media* and *Wireless* boxes are exactly where
`media-api`, `hid` and `hal` live — the libraries HP never released. The diagram
draws the boxes; in the CE release they are empty. See
[code-survey.md](code-survey.md).
