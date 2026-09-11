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

Needs a modern Debian (tested on sid) with Qt5, plus the development headers
for glib, sqlite3, openssl, libxml2 and boost.

```sh
tools/build.sh              # everything, in MANIFEST order
tools/run-lunasysmgr.sh     # start the shell
```

Each stage can be run on its own: `third-party`, `headers`, `autotools`,
`cmake`, `qmake`, `rootfs`.

The first run builds QtWebKit 5.212, which takes a while and is the only
dependency that does not live in this repository.

`tools/run-lunasysmgr.sh` **installs nothing on your system**. Only
`/etc/palm` is hardcoded in the code (`Settings.cpp`); everything else is
configurable, so `bwrap` is used to build a namespace where the paths webOS
expects point at the local rootfs.

## Layout

- `components/` — HP's sources, vendored with `git subtree --squash`. Each
  carries the source repo and sha in its import commit.
- `third-party/` — QtWebKit is **not** vendored: hundreds of MB nobody will
  edit. It is consumed at a pinned ref and built by `tools/build-third-party.sh`.
- `patches/` — portability patches. **Not from HP**: the minimum for 2012 code
  to build today. Each explains in its header what it fixes and why.
- `tests/` — small standalone programs that reproduce a specific failure without
  bringing the whole system up.
- `MANIFEST.tsv` — the 55 components with their repo, ref and build system.
- `tools/build-webos-desktop.sh.reference` — HP's original script, kept because
  **its call order is the dependency graph**, already topologically sorted. It
  is the source the new orchestration derives from.

## The two LunaSysMgrs

Both live here on purpose:

- `components/luna-sysmgr/` — **Open webOS**'s (`openwebos/luna-sysmgr`), which
  is what builds green today. The reference to compare against.
- `components/luna-sysmgr-ce/` — the **TouchPad's CE 3.0.5**
  (`woce/LunaSysMgr` at the "Push from tarball" commit), which is the target.

## State

The shell runs. `LunaSysMgr` builds with gcc 16 and Qt 5.15 and starts on X11
— see `docs/lunasysmgr-on-debian.png`.

| | |
|---|---|
| Builds from scratch | 29 components |
| Source changes over HP's drop | ~180 lines |
| Toolchain | Debian sid, gcc 16, Qt 5.15, system CMake |

Working: the lock screen, the launcher, the dock, keyboard input, taps, apps
opening as cards, db8 with its schemas loaded, and four of HP's five static
services.

`KNOWN_BUGS.md` lists what is broken, what it depends on and what has already
been ruled out — including which failures reproduce identically on HP's own
Ubuntu 12.04 build, and so are not this port's doing.

Still to do, in order:

- **The node addons** (`sysbus`, `pmlog`, `dynaload`): old v8 API, need porting
  to N-API. They are what unblocks the JS services, and with them calendar and
  email.
- **`luna-sysmgr-ce`** ported to Qt5, using as reference the 61 files where HP
  themselves added `QT_VERSION_CHECK` guards in Open webOS.
- **The browser** (`BrowserServer`/`BrowserAdapter`): these depend on real
  NPAPI, not just an include path. The most expensive piece, and last.

## Licence

HP's code is Apache 2.0; see `LICENSE` and `NOTICE`. The tag `hp-original`
marks the imported code before any change, so `git diff hp-original` shows
exactly what was modified.

Not affiliated with HP, Palm or LG.
