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

**node comes from the host**, and it is the one dependency that is not in here.
HP's own `components/nodejs` is not built: it needs Python 2 and SCons. His three
addons are, from their original sources, against `components/node-v8-shim` --
which implements node 0.4's V8 API on N-API, so they keep loading on later node
releases without recompiling. Verified on **node 26.7.0**; anything with N-API
should do. Without a `node` on `PATH` everything else still builds, and the
JavaScript services simply do not start.

```sh
tools/build.sh              # everything, in MANIFEST order
tools/run-lunasysmgr.sh     # start the shell
```

Each stage can be run on its own: `headers`, `autotools`, `cmake`, `node`,
`rootfs`. Everything lands in `build/`, and nothing outside the repository is
downloaded or compiled.

That last sentence is checked rather than asserted, and by something stronger
than a careful run on the machine it was written on:

```sh
tools/ci.sh sid        # or trixie, or both
```

builds the committed tree — `git archive HEAD`, so not the working copy — inside
a container for that Debian release, with dependencies installed first and then
**the build itself run with no network at all**, so a component reaching for a
download fails instead of quietly succeeding. It ends green: five stages, 26
components, the node addons, an assembled rootfs, and 29 of 29 tests.

It was not green to begin with, and that is the point of having it. A clean tree
on a clean system turned up eight things this repository was quietly taking from
one developer's disk — `autogen.sh` without its exec bit, then yajl, ICU,
Berkeley DB, curl, zlib and node's headers, plus a Boost package Debian had
retired. The promise on this page was false until each of them was found, and no
number of builds on the machine that wrote it would ever have said so.

`tools/run-lunasysmgr.sh` **installs nothing on your system**. Only
`/etc/palm` is hardcoded in the code (`Settings.cpp`); everything else is
configurable, so `bwrap` is used to build a namespace where the paths webOS
expects point at the local rootfs.

## Packages

```sh
tools/mkdeb.sh              # a .deb, built in an Ubuntu 26.04 container
tools/mkappimage.sh         # an AppImage, made from that .deb
```

The second wraps the first rather than building a tree of its own, so the two
artifacts are the same bits by construction and a fix present in one cannot be
missing from the other. Both are self-contained under `/opt/webos-ce`, and both
start through one process that owns the whole stack and tears it down on the
way out — `tools/webos-session.sh`. `run-lunasysmgr.sh` stays what it was, the
development tool: granular, and nothing torn down behind your back.

The AppImage needs `bubblewrap` on the host and it is not optional, because
this tree cannot be relocated. 24 of its ELF files carry `/opt/webos-ce`
compiled into them — four components generate a header from a `.in` template
holding a `WEBOS_INSTALL_*` path, and `ls-hubd` and `ls-monitor` get theirs
through `add_definitions`. Run `filecache` with that directory absent and it
opens `/opt/webos-ce/etc/palm/FileCache.conf`, takes `ENOENT` and gives up. So
`AppRun` does not move the prefix: it binds the payload there inside a private
mount namespace, which needs no privileges and leaves the host untouched.

`var/` is the other half. A squashfs is read-only and webOS writes as it runs:
the payload ships 12 directories and one file, and a session leaves 90 behind —
db8's log, the dock's pages, `used-first-card`. So the writable copy is seeded
once under `$XDG_DATA_HOME/webos-ce` and bound over the payload's own, which
also means a session's state survives restarts and outlives any one image.

### Not a Flatpak, and not for the expected reason

The obvious objection is that webOS needs six absolute paths a Flatpak sandbox
does not have, and that `bwrap` cannot nest inside one to create them. The
second half is true — `unshare` is refused inside the sandbox on four different
runtimes, with and without `--allow=devel`, while the same command succeeds on
the host — and it turns out not to matter. Flatpak already gives an app a
writable `/etc` and `/var`, so four of the six (`/etc/palm`, `/var/luna`,
`/var/palm`, `/var/db`) are one `mkdir` at startup, measured inside a real
app's sandbox rather than a bare runtime, which mounts them differently.

The two that are refused are `/usr/palm` and `/usr/lib/luna`, both under the
runtime's read-only `/usr`. `/usr/palm` appears in 68 data files, which the
build already rewrites, and in roughly 43 literals in compiled code — 20 of
them the keyboards' emoticons — but all of them are the same string, so it is a
substitution in a Flatpak build, not a patch to HP's sources. `/usr/lib/luna`
is the harder one: it holds the service binaries, and ls-hubd authorises a
caller by comparing its `/proc/<pid>/exe` against the path in a role file.

What actually decides it is QtWebEngine. `org.kde.Platform` 6.10 and 6.11 ship
its translations but not the library, and none of flathub's 2,441 runtime refs
mentions it, so a Flatpak would have to compile it inside the manifest: hours
per build, indefinitely. The `.deb` and the AppImage take it from the
distribution instead, which is the whole reason they are cheap.

## Layout

- `components/` — HP's sources, vendored with `git subtree --squash`. Each
  carries the source repo and sha in its import commit.
- `patches/` — one build-time helper script. It used to hold portability
  patches; they have all been absorbed into the components themselves, where
  `git diff hp-original` shows them in context instead of as a pile of diffs.
- `tests/` — small standalone programs that reproduce a specific failure without
  bringing the whole system up.
- `MANIFEST.tsv` — the 55 components with their repo, ref and build system.
- `docs/component-inventory.md` — which of them are built, which are data, which
  are deliberately not built and why, plus what is here that no longer has a
  use. Written against a from-scratch build, with the command behind each
  figure.
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
drag-to-scroll, scrolling with a wheel or a trackpad — which webOS itself never
had, so it is carried across HP's IPC by `components/input-compat` without
changing it — apps opening as cards, db8 with its schemas loaded, HP's
services up alongside it (`mojodb-luna`, `LunaSysService`, `filecache`,
`activitymanager`, `LunaUniversalSearchMgr`, `mojomail`), the base apps
(calendar, accounts, email) with a local profile account, a resizable shell
window the whole UI re-lays out to, and **the browser** — real page content,
clickable links, typing in the address bar and in pages, YouTube at 1080p
including fullscreen.

Not working, with the reasons measured in `KNOWN_BUGS.md`: the on-screen
keyboard (it draws, but every touch is consumed upstream before it arrives) and
the browser's padlock dialog.

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
