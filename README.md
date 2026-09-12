# webOS CE — monorepo

The code **HP released as open source** for webOS, in one repository you clone
once and build without downloading anything else.

Nothing from LuneOS, LunaCE or LG's webOS OSE. Only HP's original drop, at the
exact tags its own build script pinned.

## Why

The original build (`build-webos-desktop.sh`) does not depend on code: it
depends on **URLs**. It downloads 45 zipballs from fixed tags across three
GitHub organisations. Thirteen years later three of them are already dead:

| Download | What happened |
|---|---|
| WebKit from `github.com/downloads/...` | GitHub removed Downloads in 2013 |
| leveldb from `googlecode.com` | Google Code shut down in 2016 |
| prebuilt cmake from `cmake.org` | that binary is no longer hosted |

And that only gets worse.

## Build

Needs a modern Debian (tested on sid) with Qt 6 -- `qt6-base-dev`,
`qt6-base-private-dev`, `qt6-declarative-dev`, `qt6-declarative-private-dev`,
`qt6-webengine-dev`, `qt6-scxml-dev` -- plus the development headers for glib,
sqlite3, openssl, libxml2 and boost.

```sh
tools/build.sh              # everything, in MANIFEST order
tools/run-lunasysmgr.sh     # start the shell
```

Each stage can be run on its own: `headers`, `autotools`, `cmake`, `node`,
`rootfs`. Everything lands in `build/`, and nothing outside the repository is
downloaded or compiled.

That last sentence is checked rather than asserted. Deleting `build/` and
running the whole thing inside `bwrap --unshare-net` — no network at all, so
anything reaching for a download fails instead of quietly succeeding — takes
**11 minutes on 12 cores** and ends with all five stages green, 26 components
built, and a shell that starts. `git status` is untouched afterwards: the build
writes nothing into the sources.

`tools/run-lunasysmgr.sh` **installs nothing on your system**. Only
`/etc/palm` is hardcoded in the code (`Settings.cpp`); everything else is
configurable, so `bwrap` is used to build a namespace where the paths webOS
expects point at the local rootfs.

## Layout

- `components/` — HP's sources, vendored with `git subtree --squash`. Each
  carries the source repo and sha in its import commit.
- `patches/` — one build-time helper script. It used to hold portability
  patches; they have all been absorbed into the components themselves, where
  `git diff hp-original` shows them in context instead of as a pile of diffs.
- `tests/` — small standalone programs that reproduce a specific failure without
  bringing the whole system up.
- `MANIFEST.tsv` — the 55 components with their repo, ref and build system.
- `tools/build-webos-desktop.sh.reference` — HP's original script, kept because
  **its call order is the dependency graph**, already topologically sorted. It
  is the source the new orchestration derives from.

## The two LunaSysMgrs

- `components/luna-sysmgr/` — **Open webOS**'s (`openwebos/luna-sysmgr`). This
  is the reference implementation and the one all work happens on.
- `components/luna-sysmgr-ce/` — the **TouchPad's CE 3.0.5**
  (`woce/LunaSysMgr` at the "Push from tarball" commit). Kept for reference
  only; it is not built and will not be ported.

## State

The shell runs. `LunaSysMgr` builds with gcc 16 and Qt 6.10 and comes up as a
native Wayland client — see `docs/lunasysmgr-on-debian.png`.

| | |
|---|---|
| Listed in the manifest | 55 components |
| Marked buildable | 35 |
| Actually built | 26 — the other nine are skipped on purpose, each with its reason in `tools/build.sh` (`qt4` and `webkit` are replaced by Debian's Qt 6 and QtWebEngine; `nodejs` by Debian's node) |
| Changes inside HP's components | 230 files, +8,111 −433 — `git diff --stat hp-original -- components/` |
| …in files HP never shipped | 6,740 lines: the adapters below |
| …inside HP's own files | 1,804 lines |
| Toolchain | Debian sid, gcc 16, Qt 6.10 + QtWebEngine, system CMake |

`git diff --stat hp-original` on the whole tree reports a much larger number —
it counts `tools/`, `tests/` and these documents too, and it moves every time
one of them is edited. The figures above are scoped to `components/` on
purpose: what changed in HP's code is the number worth being able to check.

The split matters more than the total: most of the work is **new code beside
HP's**, not edits to it. Three adapters carry it — `qt6-compat` for what Qt 6
removed, `qtwebkit-compat` for the QtWebKit API WebAppMgr is written against
(on QtWebEngine), and `node-v8-shim` for HP's three node addons on node 26.

Working: the lock screen, the launcher, the dock, keyboard input, taps and
drag-to-scroll, apps opening as cards, db8 with its schemas loaded, HP's
services up alongside it (`mojodb-luna`, `LunaSysService`, `filecache`,
`activitymanager`, `LunaUniversalSearchMgr`, `mojomail`), the base apps
(calendar, accounts, email) with a local profile account, a resizable shell
window the whole UI re-lays out to, and **the browser** — real page content,
clickable links, typing in the address bar and in pages, YouTube at 1080p
including fullscreen.

Not working, with the reasons measured in `KNOWN_BUGS.md`: the on-screen
keyboard (it draws, but every touch is consumed upstream before it arrives),
trackpad swipe scrolling (nothing in the stack carries a wheel event), and the
browser's padlock dialog.

`KNOWN_BUGS.md` lists what is broken, what it depends on and what has already
been ruled out — including which failures reproduce identically on HP's own
Ubuntu 12.04 build, and so are not this port's doing.

Still to do, in order:

- ~~**The node addons** (`sysbus`, `pmlog`, `dynaload`)~~: done, unmodified,
  through `components/node-v8-shim` on node 26. HP's JavaScript services start
  on demand, which is what lets apps have background services.
- ~~**Qt 6**~~: done, and the only build there is. What Qt 6 removed comes back
  through `components/qt6-compat`; the QtWebKit API WebAppMgr is written against
  comes back through `components/qtwebkit-compat`, on QtWebEngine.
- ~~**The browser**~~: done, and not the way HP did it. `BrowserServer` and the
  NPAPI `BrowserAdapter` (~29k lines) are replaced by a `QWebPage::embedPage`
  that paints one page inside another, plus `BrowserViewAdapter` speaking to
  QtWebEngine directly.
- **Native touch**: the engine side is proven (a synthetic `QTouchEvent`
  scrolls Chromium), but it has to be routed through `deliverToEmbedded` before
  the pen events can be suppressed, or the browser loses its scroll. Tried,
  reverted, written up.
- **Synergy** (accounts and their transports, mojomail): last. The local
  profile account exists, which is what the base apps needed.

## Licence

HP's code is Apache 2.0; see `LICENSE` and `NOTICE`. The tag `hp-original`
marks the imported code before any change, so `git diff hp-original` shows
exactly what was modified.

Not affiliated with HP, Palm or LG.
