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

`tools/run-lunasysmgr.sh` **installs nothing on your system**. Only
`/etc/palm` is hardcoded in the code (`Settings.cpp`); everything else is
configurable, so `bwrap` is used to build a namespace where the paths webOS
expects point at the local rootfs.

## Layout

- `components/` — HP's sources, vendored with `git subtree --squash`. Each
  carries the source repo and sha in its import commit.
- `patches/` — portability patches. **Not from HP**: the minimum for 2012 code
  to build today. Each explains in its header what it fixes and why.
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

The shell runs. `LunaSysMgr` builds with gcc 16 and Qt 6.10 and starts on X11
— see `docs/lunasysmgr-on-debian.png`.

| | |
|---|---|
| Builds from scratch | 29 components |
| Source changes over HP's drop | ~180 lines |
| Toolchain | Debian sid, gcc 16, Qt 6.10 + QtWebEngine, system CMake |

Working: the lock screen, the launcher, the dock, keyboard input, taps, apps
opening as cards, db8 with its schemas loaded, and four of HP's five static
services.

`KNOWN_BUGS.md` lists what is broken, what it depends on and what has already
been ruled out — including which failures reproduce identically on HP's own
Ubuntu 12.04 build, and so are not this port's doing.

Still to do, in order:

- ~~**The node addons** (`sysbus`, `pmlog`, `dynaload`)~~: done, unmodified,
  through `components/node-v8-shim` on node 26. HP's JavaScript services start
  on demand, which is what lets apps have background services.
- ~~**Qt 6**~~: done, and the only build there is. What Qt 6 removed comes back
  through `components/qt6-compat`; the QtWebKit API WebAppMgr is written against
  comes back through `components/qtwebkit-compat`, on QtWebEngine. That also
  covers what the browser path (`BrowserServer`/`BrowserAdapter`/
  `WebKitSupplemental`) existed for: rendering pages in another process.
- **Synergy** (accounts and their transports, mojomail): last.

## Licence

HP's code is Apache 2.0; see `LICENSE` and `NOTICE`. The tag `hp-original`
marks the imported code before any change, so `git diff hp-original` shows
exactly what was modified.

Not affiliated with HP, Palm or LG.
