# Component inventory: what is here, what is used, what is not

Measured on 2026-09-12, after a from-scratch offline build. Every figure below
comes with the command that produces it, because a number nobody can re-derive
stops being true without anybody noticing.

`components/` holds **56 directories**: 51 of the 55 entries in `MANIFEST.tsv`,
plus 5 that are not manifest components. Four manifest entries have no directory
at all: `cmake` and `leveldb` are marked EXTERNO, and `qt4` and `webkit` were
never vendored because Debian's Qt 6 and QtWebEngine replace them.

    awk -F'\t' 'NR>1 {print $2"\t"$5}' MANIFEST.tsv    # the manifest
    ls -d components/*/ | wc -l                        # the directories

## Built: 26

`cjson` through autotools; the rest through CMake:

    activitymanager  configurator  db8  filecache  jemalloc  keyboard-efigs
    libpalmsocket  librolegen  libsandbox  luna-init  luna-prefs  luna-service2
    luna-sysmgr  luna-sysmgr-common  luna-sysmgr-ipc  luna-sysmgr-ipc-messages
    luna-sysservice  luna-universalsearchmgr  mojomail  mojoservicelauncher
    nyx-lib  pbnjson  pmloglib  pmstatemachineengine  webappmanager  cjson

35 entries are marked `cmake`/`qmake` in the manifest; the nine that are marked
and not built are listed next, each with its reason in `tools/build.sh`'s SKIP
table. That is where to look first, not here: the table is next to the code that
honours it.

## Marked buildable, deliberately not built

| Component | Size | Why |
|---|---|---|
| `WebKitSupplemental` | 38M | NPAPI browser path. QtWebEngine covers what it did |
| `BrowserServer` | 824K | same — replaced by `QWebPage::embedPage` + `BrowserViewAdapter` |
| `BrowserAdapter` | 656K | same — it *is* the NPAPI plugin, and Chromium has no socket for it |
| `AdapterBase` | 280K | same |
| `nodejs` | 58M | HP's node needs Python 2 and SCons; the host's node is used |
| `nodejs-module-webos-sysbus` | 184K | **sources are built** — see below |
| `nodejs-module-webos-pmlog` | 36K | **sources are built** — see below |
| `nodejs-module-webos-dynaload` | 60K | **sources are built** — see below |
| `cmake-modules-webos` | 120K | CMake modules, consumed through `CMAKE_MODULE_PATH` |

### The three node addon components are load-bearing

Easy to get wrong, and worth stating plainly: what is skipped is **HP's
CMakeLists for each of them**, not their code. `components/node-v8-shim/addons`
compiles their sources directly:

    ${COMPONENTS}/nodejs-module-webos-pmlog/src/pmloglib.cpp
    ${COMPONENTS}/nodejs-module-webos-sysbus/src/node_ls2*.cpp        (7 files)
    ${COMPONENTS}/nodejs-module-webos-dynaload/src/{node_webos,external_string}.cpp

Deleting those directories breaks the build. They are not spare.

## Data and JavaScript: 17 `copiar` components

Fifteen are installed into the rootfs by `tools/assemble-rootfs.sh`:
`luna-applauncher`, `luna-systemui`, `enyo-1.0`, `core-apps`, `isis-browser`,
`isis-fonts`, `foundation-frameworks`, `mojoservice-frameworks`,
`loadable-frameworks`, `app-services`, `mojolocation-stub`, `underscore`,
`mojoloader`.

`pmnetconfigmanager-stub` used to be on that list and no longer is. It answers
`com.palm.connectionmanager/getStatus` with a constant -- connected, over wifi,
on "Open webOS", always -- and `components/nm-connectionmanager` now answers that
name from NetworkManager instead. The component stays vendored, because
MANIFEST.tsv is an inventory of what HP released, but nothing copies it into the
rootfs.

The other two are used at build time rather than install time, which is why a
grep of `assemble-rootfs.sh` makes them look unused:

- `luna-webkit-api` — the IME headers. 18 files in the shell and WebAppMgr
  include them; the headers stage copies 7 into staging.
- `npapi-headers` — HP's sources include `<npapi.h>` from places that are not
  the browser path, so 4 headers are still copied even though NPAPI is gone.

## Not in the manifest

- `build-support-ce` — **76M, 9,108 tracked files, zero references** anywhere in
  `tools/` or any `CMakeLists`. A prebuilt staging tree for **ARM**: useless on
  x86. The largest dead weight here.
- `luna-sysmgr-ce` — 21M, the TouchPad's LunaSysMgr. Reference only, not built,
  and byte-identical to HP's drop (`git diff hp-original` on it is empty). It is
  the evidence for where `webkitView()` and `Palm::WebView` actually lived,
  which is why it is kept and why it is not edited.
- `node-v8-shim`, `qt6-compat`, `qtwebkit-compat` — ours, not HP's. The three
  adapters the port rests on.

## Applications and services

**8 apps installed**, and that is all of them: the 7 in `core-apps`
(`accounts`, `calculator`, `calendar`, `clock`, `contacts`, `email`, `notes`)
plus the browser from `isis-browser`. Nothing in `core-apps` is left out.

**Services**: 7 directories under `usr/palm/services` — `com.palm.service.accounts`,
`com.palm.service.calendar.reminders`, `com.palm.service.contacts`,
`com.palm.service.contacts.linker` (from `app-services`),
`com.palm.connectionmanager` and `com.palm.location` (from the two stubs), and
`jsservicelauncher`.

`app-services/account-templates` is not a service and is installed elsewhere
(it carries `palmprofile`, the account template). `app-services/mojomail` is
skipped correctly: the install loop requires a `services.json` or `package.json`
and it has neither — the mail services are the C++ ones.

**mojomail's three daemons are wired.** `mojomail-imap`, `mojomail-pop` and
`mojomail-smtp` are installed in `/usr/lib/luna` with their bus files picked up
from `components/mojomail/desktop-support/` by the `*/desktop-support` loop:
`com.palm.{imap,pop,smtp}.service` in `system-services/` and their roles in
`roles/prv/`. They exist only in the private (`.prv`) variant, so their absence
from `services/` and `roles/pub/` is correct rather than a gap.

## node is pinned and shipped

HP's `components/nodejs` is not built: it needs Python 2 and SCons. The official
node LTS is used instead, pinned by version and SHA-256 in `tools/node-version`
(24.21.0 today). `tools/fetch-node.sh` downloads it and refuses a tarball that
does not match the pin; it is the one step that needs the network, and the
build never does.

That node is used everywhere: the addons compile against its headers, the tests
run on it, and `assemble-rootfs.sh` copies the binary to `/usr/palm/nodejs/node`
-- the path HP's own bus role file and `run-js-service` already look for. It
used to be an empty file the launcher bind-mounted the host's node onto, so the
`.deb` depended on the distribution's node and the AppImage used whatever the
host had, or none.

`components/node-v8-shim` implements node 0.4's V8 API on **N-API**, which is
ABI-stable. MEASURED: the built addons import only `napi_*` (and `uv_*`) symbols,
none from `v8::` or `node::`, and load under both node 24 and 26 -- so moving the
pin to a newer LTS does not mean rebuilding them for a new ABI.

## Why the unused components stay

Deleting them would contradict the reason this repository exists. HP's build
depended on URLs, and three of them are already dead (GitHub Downloads,
googlecode, the pinned cmake binary). `WebKitSupplemental`, `BrowserServer`,
`BrowserAdapter` and `nodejs` are the original drop, which nobody else is
keeping.

And deleting them would not buy what it looks like it buys. `build-support-ce`'s
**8,969 objects are already in history**: removing the directory shrinks the
working tree by 76M and leaves a fresh clone the same size. `.git` is 168M, and
its largest blobs are not that directory at all —

    components/nodejs/deps/v8/test/cctest/gay-shortest.cc     6.1M
    components/nodejs/deps/v8/test/cctest/gay-precision.cc    5.9M
    components/nodejs/deps/v8/test/cctest/gay-fixed.cc        5.6M
    components/WebKitSupplemental/qbsplugin/fonts/*.ttf      ~14M

— so a smaller clone means rewriting history, which is a different decision with
a different cost.

## Traps when measuring any of this

Three of these cost real time while this inventory was being written. Each time
the tool answered a badly-formed question exactly, and the empty result looked
like a finding:

- **`grep -i mail`** finds none of mojomail's bus files. They are named
  `com.palm.imap`, `com.palm.pop` and `com.palm.smtp` — no "mail" in any of
  them. This is what produced a confident, wrong report that three daemons could
  never be launched.
- **`pgrep -x <name>`** silently matches nothing when the name is longer than 15
  characters, and warns on stderr. `LunaUniversalSearchMgr` is 22. Use `pgrep -f`
  or match on the executable path.
- **`pkill -f <pattern>`** matches your own shell's command line when the
  pattern appears in it, so the command kills itself. Killing engine processes
  this way took down the script doing the killing, twice.

Their shape is the same: an absence is not evidence. Check that the question
could have returned a positive result before believing a negative one.
