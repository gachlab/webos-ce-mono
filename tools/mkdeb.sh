#!/bin/bash
# Builds a .deb for Ubuntu 26.04 LTS, inside a container.
#
#   tools/mkdeb.sh              # build the image if needed, then the package
#
# The package lands in dist/. The host needs nothing but docker or podman: the
# compiler, Qt and dpkg-dev all live in the image, which is the point -- the
# binaries have to link against the target distribution's Qt, not against sid's.
#
# Why a fixed prefix and not a relocatable tarball: dpkg installs to one place
# and the ls2 bus wants absolute paths in its .service and role files. So the
# tree is assembled with WEBOS_PREFIX=/opt/webos-ce and everything inside it
# names that. The binaries keep their $ORIGIN rpaths, so the same tree is still
# relocatable -- the package just chooses not to move it.
#
# Layout of the installed package:
#
#   /opt/webos-ce/bin/webos-ce       the entry point: sets the paths, execs
#                                    tools/run-lunasysmgr.sh
#   /opt/webos-ce/tools/             this repo's run script, unmodified
#   /opt/webos-ce/usr, etc, var      the rootfs, exactly as assemble-rootfs.sh
#                                    builds it
#
# Built from `git archive HEAD`, like tools/ci.sh: a package should contain what
# is committed, not whatever is lying around in the working tree.
set -u

R="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX=/opt/webos-ce
IMAGE=webos-ce-pkg:ubuntu-26.04
DIST="$R/dist"

RUNNER="$(command -v podman || command -v docker || true)"
if [ -z "$RUNNER" ]; then
    echo "mkdeb: neither podman nor docker on PATH" >&2
    exit 2
fi

# Version: upstream is HP's 3.0.5 drop, and everything after it is ours. The
# commit count keeps packages ordered by dpkg's own comparison, and the short
# sha says exactly what is in it.
# Refuse to package a working tree that does not match HEAD, and say which files
# differ. The build feeds the container `git archive HEAD` -- HEAD being the tip
# of whatever branch you are on -- so uncommitted edits are silently left out,
# and the version below embeds the sha, which would then name a commit that does
# not contain what is in the package.
#
# This is not hypothetical. A package built here while tools/build.sh had
# uncommitted fixes came out without them: the container got the committed copy,
# the build reported success, and it took inspecting the artifact -- filecache
# still carrying /src/build/staging, palmbus.node with no rpath -- to notice.
# Seven minutes to build and nothing said a word. An immediate, loud failure is
# cheaper than a quiet wrong one.
#
# MKDEB_ALLOW_DIRTY=1 overrides it, for when packaging HEAD while holding
# unrelated edits is what you actually mean.
if [ -z "${MKDEB_ALLOW_DIRTY:-}" ] \
   && ! (git -C "$R" diff --quiet && git -C "$R" diff --cached --quiet); then
    echo "mkdeb: the working tree differs from HEAD, which is what would be packaged:" >&2
    git -C "$R" status --short >&2
    echo "mkdeb: commit them, or set MKDEB_ALLOW_DIRTY=1 to package HEAD anyway." >&2
    exit 1
fi

UPSTREAM=3.0.5
COUNT="$(git -C "$R" rev-list --count HEAD)"
SHA="$(git -C "$R" rev-parse --short HEAD)"
VERSION="$UPSTREAM-0+$COUNT.$SHA"

# Same list as tools/ci.sh, plus what only the packaging needs. Kept here rather
# than sourced from there because the two answer different questions: ci.sh asks
# "does it build on Debian", this asks "what does the release image need".
read -r -d '' PACKAGES <<'PKGS'
build-essential cmake pkg-config
autoconf automake libtool
python3
qt6-base-dev qt6-base-private-dev
qt6-declarative-dev qt6-declarative-private-dev
qt6-webengine-dev qt6-scxml-dev
libglib2.0-dev libglibmm-2.4-dev libsigc++-2.0-dev
libsqlite3-dev libssl-dev libxml2-dev libyajl-dev libicu-dev
libdb5.3-dev libcurl4-openssl-dev zlib1g-dev
libboost-filesystem-dev libboost-regex-dev libboost-program-options-dev
libc-ares-dev liburiparser-dev
nodejs libnode-dev
dpkg-dev fakeroot
PKGS

build_image() {
    if "$RUNNER" image inspect "$IMAGE" >/dev/null 2>&1; then
        echo "== image $IMAGE already present =="
        return 0
    fi
    echo "== building $IMAGE =="
    printf 'FROM ubuntu:26.04\nENV DEBIAN_FRONTEND=noninteractive\nRUN apt-get update && apt-get install -y --no-install-recommends %s && rm -rf /var/lib/apt/lists/*\n' \
        "$(echo "$PACKAGES" | tr '\n' ' ')" \
        | "$RUNNER" build -t "$IMAGE" -f - "$R" > /tmp/webos-mkdeb-image.log 2>&1
    if [ $? -ne 0 ]; then
        echo "  FAILED; what apt said:"
        # apt's real error is near the top, not at the end: the tail is only the
        # RUN line echoed back. Same trap as tools/ci.sh documents.
        grep -hE "^(E|W): |Unable to locate|no installation candidate|no space" \
            /tmp/webos-mkdeb-image.log | head -10 | sed 's/^/    /'
        return 1
    fi
    echo "  ok"
}

# Everything below runs inside the container. It is a separate file rather than
# an -c string so that the quoting stays readable and the shell inside is not
# fighting the shell outside.
write_inner() {
    cat > "$1" <<INNER
#!/bin/sh
set -e
PREFIX=$PREFIX
VERSION=$VERSION
INNER
    cat >> "$1" <<'INNER'
mkdir -p /src && tar -x -C /src
cd /src

echo "== build =="
# WEBOS_PREFIX is what gets COMPILED IN, and it is the whole reason the build
# runs this way. Four components generate a header from a .in template carrying
# a WEBOS_INSTALL_* path, and ls-hubd and ls-monitor get theirs through
# add_definitions; building with the prefix set to a staging directory shipped
# binaries that looked for /src/build/staging at runtime. filecache died on it
# outright and configurator could not record anything it configured.
#
# No DESTDIR. The container is disposable, so the build installs straight to the
# real $PREFIX and every path agrees with every other: the paths compiled into
# the binaries, the ones in the generated .pc files, and the -I and -L the next
# component is given are all the same directory, because they are.
#
# Staging it under a DESTDIR was tried first and does not work here. The .pc
# files and our own CMakeLists name the prefix absolutely, so with the files at
# /stage/opt/webos-ce every consumer looked in /opt/webos-ce, which did not
# exist: luna-service2 failed pkg-config and eleven components after it failed
# on SysMgrEvent.h, lunaservice.h and nyx_client.h. DESTDIR is for installing
# into a system you are not allowed to write to. In a throwaway container we are.
WEBOS_PREFIX="$PREFIX" tools/build.sh

PKG=/pkg
ROOT="$PKG$PREFIX"
mkdir -p "$ROOT"
echo "== assemble for $PREFIX =="
WEBOS_STAGING="$PREFIX" \
WEBOS_PREFIX="$PREFIX" \
WEBOS_LAUNCHER="$PREFIX/bin/webos-ce" \
WEBOS_BINDIR="$PREFIX/usr/lib/luna" \
WEBOS_SBINDIR="$PREFIX/usr/lib/luna" \
    tools/assemble-rootfs.sh "$ROOT"

# The launcher itself, plus a wrapper that tells it where it lives.
#
# run-lunasysmgr.sh derives its paths from its own location ("$0/.."), which is
# right in a build tree and wrong in /opt: it would look for build/rootfs under
# the prefix. The wrapper supplies the four paths instead, and is what the 34
# .service files name as their Exec -- so this is also what ls-hubd runs, and
# bwrap passes the environment through when the script re-enters itself.
mkdir -p "$ROOT/tools" "$ROOT/bin"
cp -f tools/run-lunasysmgr.sh tools/webos-session.sh "$ROOT/tools/"
cat > "$ROOT/bin/webos-ce" <<WRAP
#!/bin/sh
# Entry point for the packaged webOS CE. Generated by tools/mkdeb.sh.
export WEBOS_ROOTFS="$PREFIX"
export WEBOS_STAGING="$PREFIX"
export WEBOS_BINDIR="$PREFIX/usr/lib/luna"
export WEBOS_SBINDIR="$PREFIX/usr/lib/luna"
export WEBOS_LAUNCHER="$PREFIX/tools/run-lunasysmgr.sh"

# This dispatches, and it has to.
#
# It is not only what a user types: ls-hubd starts services on demand through
# it, with Exec=.../bin/webos-ce js-service ... and ns-exec ... written into all
# 34 .service files. Sending everything to the session supervisor would mean
# every on-demand service launch brought up an entire stack of its own.
#
# So the launcher's own subcommands go straight through, and only a bare
# invocation -- what a person runs -- gets the supervisor that owns the whole
# lifecycle and tears it down on the way out.
case "\${1:-}" in
    bus|services|init|run|stop|ns-exec|js-service)
        exec "\$WEBOS_LAUNCHER" "\$@" ;;
    *)
        exec "$PREFIX/tools/webos-session.sh" "\$@" ;;
esac
WRAP
chmod 0755 "$ROOT/bin/webos-ce" "$ROOT/tools/run-lunasysmgr.sh" \
           "$ROOT/tools/webos-session.sh"

# webOS writes under var/ at runtime, and the package is installed by root while
# the shell runs as an ordinary user. 1777 is the honest minimum for a
# self-contained tree under /opt; the cleaner answer is a per-user copy under
# ~/.local/share, which needs the launcher to learn one more path.
#
# EVERY directory, not just these three. An earlier version named var/db,
# var/luna and var/palm and stopped there, which left their children -- the ones
# actually written to -- at 0755 root. That is what crashed the shell:
# CardWindowManager::markFirstCardDone does
#
#     fopen("/var/luna/preferences/used-first-card", "w"); fclose(f);
#
# with no NULL check, and that path is a hardcoded literal in Settings.cpp with
# no configuration key, so it cannot be pointed anywhere else. Unwritable
# directory, fopen returns NULL, fclose(NULL) segfaults, WebAppMgr disconnects
# and the whole stack follows. Caught under gdb with `f = 0x0` in frame 1.
find "$ROOT/var" -type d -exec chmod 1777 {} + 2>/dev/null || true

echo "== dependencies =="
# dpkg-shlibdeps reads the binaries and names the exact packages that provide
# their sonames -- which is the only way to get this right: libicu's package
# name carries its version (libicu76 today) and hardcoding it would be wrong by
# the next release.
#
# -l points it at our own libraries so it does not report them as missing
# system ones, and --ignore-missing-info keeps it from failing on the few that
# ship no shlibs file.
mkdir -p debian
printf 'Source: webos-ce\n\nPackage: webos-ce\nArchitecture: amd64\nDepends: ${shlibs:Depends}\n' > debian/control
# The libraries as well as the executables. dpkg-shlibdeps reads only the
# NEEDED entries of the files it is handed, so passing the binaries alone would
# miss every dependency that arrives through a library instead -- boost and
# curl reach this package through mojomail's libraries, which no executable
# links directly. The result would be a Depends: that looks right and fails on
# a machine that does not already have them.
BINS=$(find "$ROOT/usr/lib/luna" -maxdepth 1 -type f -executable; \
       find "$ROOT/usr/lib" -maxdepth 1 -type f -name '*.so*')
SHLIBDEPS=$(dpkg-shlibdeps -O --ignore-missing-info -l"$ROOT/usr/lib" $BINS 2>/dev/null \
            | sed 's/^shlibs:Depends=//')
# bubblewrap and node are run, not linked, so nothing above can find them.
# Without bwrap the launcher cannot start at all; without node the JavaScript
# services never come up, which reads like a dozen unrelated bugs.
DEPENDS="bubblewrap, nodejs${SHLIBDEPS:+, $SHLIBDEPS}"

echo "== control =="
mkdir -p "$PKG/DEBIAN"
INSTALLED_KB=$(du -sk "$ROOT" | cut -f1)
cat > "$PKG/DEBIAN/control" <<CTRL
Package: webos-ce
Version: $VERSION
Section: misc
Priority: optional
Architecture: amd64
Maintainer: webos-ce-mono <nobody@localhost>
Installed-Size: $INSTALLED_KB
Depends: $DEPENDS
Description: HP webOS Community Edition for modern Linux
 The webOS 3.0.5 Community Edition shell, applications and services, built
 against a current Qt 6 and QtWebEngine instead of the Qt 4 and QtWebKit the
 original drop required.
 .
 Installs self-contained under $PREFIX. Start it with $PREFIX/bin/webos-ce.
CTRL

# A postinst, because the modes in the archive are not enough on an upgrade.
# dpkg does not change the permissions of a directory that already exists, so a
# machine that had an earlier package kept var/luna/preferences at 0755 root
# while the new archive carried 1777 -- measured: var/luna and var/db came out
# right only because the previous package had already made them so. The one
# directory that was wrong is the one webOS writes its first-card marker into,
# which is what made the shell segfault, so this cannot be left to chance.
cat > "$PKG/DEBIAN/postinst" <<'POST'
#!/bin/sh
set -e
if [ "$1" = configure ]; then
    find /opt/webos-ce/var -type d -exec chmod 1777 {} + 2>/dev/null || true
fi
POST
chmod 0755 "$PKG/DEBIAN/postinst"

echo "== build the package =="
dpkg-deb --build --root-owner-group "$PKG" "/out/webos-ce_${VERSION}_amd64.deb"
INNER
}

build_image || exit 1

mkdir -p "$DIST"
INNER_SH=/tmp/webos-mkdeb-inner.sh
write_inner "$INNER_SH"

echo "== building the package in $IMAGE =="
# The source goes in on stdin and the .deb comes out through /out. The build
# itself has no network: a component that reaches for a download has to fail
# here rather than succeed quietly on a machine that happens to be online.
#
# Never pipe this into tail: the pipeline's status would be tail's, and a failed
# build would report success. tools/ci.sh has the same warning for the same
# reason.
git -C "$R" archive --format=tar HEAD > /tmp/webos-mkdeb-src.tar
if ! "$RUNNER" run --rm -i --network none \
        -e QTWEBENGINE_CHROMIUM_FLAGS="--no-sandbox --disable-gpu" \
        -v "$DIST:/out" \
        -v "$INNER_SH:/inner.sh:ro" \
        -w /src "$IMAGE" \
        sh /inner.sh < /tmp/webos-mkdeb-src.tar > /tmp/webos-mkdeb.log 2>&1; then
    echo "mkdeb: FAILED. The failure:" >&2
    grep -nE "error|Error|FAILED|No package .* found|None of the required|cannot find" \
        /tmp/webos-mkdeb.log | head -15 | sed 's/^/  /' >&2
    echo "  (full log: /tmp/webos-mkdeb.log)" >&2
    exit 1
fi

# dpkg-deb wrote it through the bind mount as root, so the file on the host is
# root-owned and mode 0644. That is fine: it is readable, and installing it
# needs sudo either way. Running the container as the host's uid instead is not
# the fix -- /src is not writable by it, which is what tools/ci.sh measured.
DEB="$DIST/webos-ce_${VERSION}_amd64.deb"

echo
echo "built: $DEB"
ls -lh "$DEB" 2>/dev/null | awk '{print "  size: " $5}'
echo "  contents:"
dpkg-deb --info "$DEB" 2>/dev/null | grep -E "^ (Version|Depends|Installed-Size)" | sed 's/^/  /'
echo
echo "Install with:  sudo dpkg -i $DEB"
echo "Start with:    $PREFIX/bin/webos-ce"
