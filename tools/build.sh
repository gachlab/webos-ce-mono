#!/bin/bash
# Builds all of webOS on a modern Debian, in MANIFEST order, against Qt 6.
#
# The order is not ours: it is HP's build-webos-desktop.sh, which already came
# topologically sorted. MANIFEST.tsv keeps it in its first column.
#
# One build tree, build/. There is no Qt 5 build any more: Qt 6 covers the shell
# through components/qt6-compat and WebAppMgr through components/qtwebkit-compat,
# on Debian's QtWebEngine, so nothing here builds QtWebKit 5.212.
#
# Usage:
#   tools/build.sh              # everything
#   tools/build.sh cmake        # one stage: headers | autotools | cmake |
#                               # node | powerd | connmgr | storaged | rootfs
set -u
R="$(cd "$(dirname "$0")/.." && pwd)"
STAGE="${1:-all}"
B="$R/build"

# Where the tree will RUN is not where the build WRITES it, and four components
# compile the difference into their binaries. filecache, configurator,
# librolegen and libsandbox each generate a header from a .in template that
# substitutes a WEBOS_INSTALL_* path, and ls-hubd and ls-monitor get theirs
# through add_definitions -- so whatever WEBOS_INSTALL_ROOT says at configure
# time is burned into the executable.
#
# With both set to the staging directory, as they were, a package built in a
# container shipped binaries looking for /src/build/staging: filecache died with
# "Failed to create cache directory '/src/build/staging/var/file-cache'" and
# configurator could not record a single configuration it had just applied.
#
#   WEBOS_PREFIX  the runtime prefix, burned into the binaries
#   DESTDIR       where `make install` actually writes
#   S             the staging tree the rest of the build consumes, which is the
#                 two concatenated
#
# The defaults are exactly what this script did before -- prefix is the staging
# directory and DESTDIR is empty, so S is unchanged and a developer's build is
# bit-for-bit what it was.
WEBOS_PREFIX="${WEBOS_PREFIX:-$B/staging}"
DESTDIR="${DESTDIR:-}"
S="$DESTDIR$WEBOS_PREFIX"
BUILD_TYPE="${BUILD_TYPE:-Debug}"

# Components the MANIFEST lists as cmake/qmake that we do NOT build, and why.
# They are listed here instead of being removed from the MANIFEST so that the
# manifest stays a faithful inventory of what HP released.
declare -A SKIP=(
    [cmake]="it is the tool itself; we use the system one"
    [cmake-modules-webos]="CMake modules, consumed via CMAKE_MODULE_PATH"
    [qt4]="replaced by Debian's Qt 6"
    [webkit]="replaced by QtWebEngine; WebAppMgr reaches it through components/qtwebkit-compat"
    [nodejs]="the official node LTS ships instead (tools/node-version); HP's needs Python 2 and SCons"
    # The three addons are built from components/node-v8-shim/addons, which
    # compiles HP's sources against the shim. Their own CMakeLists are HP's and
    # need a node that no longer exists.
    [nodejs-module-webos-sysbus]="built via components/node-v8-shim/addons"
    [nodejs-module-webos-pmlog]="same"
    [nodejs-module-webos-dynaload]="same"
    # The browser path renders pages in another process for a WebKit that had no
    # process of its own. Chromium already does that, and QtWebEngine has no
    # NPAPI to load BrowserAdapter into.
    [WebKitSupplemental]="browser path (NPAPI); QtWebEngine covers what it did"
    [AdapterBase]="same"
    [BrowserServer]="same"
    [BrowserAdapter]="same"
    # NOTE: db8 configures and builds without leveldb, and Debian does not
    # package it. Still to confirm at runtime whether mojodb-luna needs that
    # backend or ships another.
    [leveldb]="db8 builds without it; still to verify at runtime"
)

list_of() {  # list_of <build-system> -> names in MANIFEST order
    # The four components the MANIFEST marks as qmake have a CMakeLists.txt of
    # their own now, so they are built with everything else. Their position in
    # the MANIFEST already puts them after what they depend on.
    #
    # One change to HP's order: luna-prefs goes before luna-sysmgr. Our shell
    # links it (DeviceInfo asks it whether the machine has wifi), which HP's
    # did not, and HP's order builds it four components later. Every build from
    # a clean tree failed on lunaprefs.h. luna-prefs itself needs only cjson,
    # glib, luna-service2 and sqlite, all earlier.
    if [ "$1" = cmake ]; then
        awk -F'\t' 'NR>1 && ($5=="cmake" || $5=="qmake") {
            if ($2 == "luna-prefs") next
            if ($2 == "luna-sysmgr") print "luna-prefs"
            print $2
        }' "$R/MANIFEST.tsv"
    else
        awk -F'\t' -v s="$1" 'NR>1 && $5==s {print $2}' "$R/MANIFEST.tsv"
    fi
}

selected() {
    for c in $(list_of "$1"); do
        [ -n "${SKIP[$c]:-}" ] && continue
        # mojomail is not one component: it is four CMake projects under the
        # same directory, with no CMakeLists on top. HP builds them one by one,
        # and 'common' has to go first because the other three link against it.
        if [ "$c" = mojomail ]; then
            echo mojomail/common mojomail/imap mojomail/pop mojomail/smtp
            continue
        fi
        echo "$c"
    done
}

stage_headers() {
    echo "== headers =="
    # Three components are headers only: they are not built, just copied into
    # staging. HP does this line by line in its script and it was missing here
    # entirely, so luna-sysmgr, keyboard-efigs and webappmanager could not find
    # Common.h or palmimedefines.h. Only shows up on a clean build: once
    # copied, they survive rebuilds.
    mkdir -p "$S/include/luna-sysmgr-common" "$S/include/ime" "$S/include/webkit/npapi"

    cp -f "$R"/components/luna-sysmgr-common/include/* "$S/include/luna-sysmgr-common/" 2>/dev/null
    printf "  %-22s %s\n" "luna-sysmgr-common" "$(ls "$S/include/luna-sysmgr-common" | wc -l) headers"

    cp -f "$R"/components/luna-webkit-api/include/public/ime/*.h "$S/include/ime/" 2>/dev/null
    cp -f "$R"/components/luna-webkit-api/*.h                    "$S/include/ime/" 2>/dev/null
    printf "  %-22s %s\n" "luna-webkit-api" "$(ls "$S/include/ime" | wc -l) headers"

    # npapi-headers is still copied: HP's sources include <npapi.h> from places
    # the browser path is not the only user of.
    cp -f "$R"/components/npapi-headers/*.h "$S/include/webkit/npapi/" 2>/dev/null
    printf "  %-22s %s\n" "npapi-headers" "$(ls "$S/include/webkit/npapi" | wc -l) headers"
}

stage_autotools() {
    echo "== autotools =="
    for c in $(selected autotools); do
        d=$B/$c
        # Wiped first, as the CMake stage does. Re-running configure with new
        # LDFLAGS does NOT relink: the .lo files are unchanged, so make has
        # nothing to do and the old library stays. That cost an hour of reading
        # a stale rpath off disk and concluding the escaping was wrong when it
        # was right. A stage that reconfigures has to rebuild.
        rm -rf "$d"; mkdir -p "$d"; cd "$d" || { echo "$c: no directory"; return 1; }
        # cjson ships autogen.sh; HP's tree has no generated ./configure.
        #
        # Run it through sh rather than executing it: autogen.sh has no exec bit
        # in HP's drop, so ./autogen.sh is "Permission denied" on a fresh clone.
        # That went unnoticed for as long as the build only ever ran on a machine
        # where a previous run had already left ./configure behind -- the guard
        # above then skipped autogen entirely. CI on a clean tree found it on its
        # first run.
        #
        # And its output is kept: silencing it with >/dev/null is what turned a
        # one-line permission error into "cjson FAILED" with nothing to read.
        if [ ! -x "$R/components/$c/configure" ]; then
            (cd "$R/components/$c" && sh ./autogen.sh) > "$d/autogen.log" 2>&1 || {
                printf "%-22s FAILED autogen: %s\n" "$c" "$(tail -1 "$d/autogen.log" | cut -c1-60)"
                return 1
            }
        fi
        # The rpath CMake components get from CMAKE_INSTALL_RPATH has to be
        # handed to autotools by hand, or cjson ends up the only library in the
        # tree without one.
        #
        # \$$ORIGIN, and it takes all three characters: the string has to survive
        # two expansions before it reaches the linker.
        #   configure writes LDFLAGS into the Makefile verbatim
        #   make turns  \$$ORIGIN  into  \$ORIGIN
        #   libtool evals the link line, and the \ is what stops the shell from
        #   expanding $ORIGIN as an (empty) variable
        # Getting this wrong is silent: a plain $ORIGIN leaves "RIGIN:RIGIN/.."
        # and $$ORIGIN leaves ":/..", both of which link fine and only fail once
        # the tree is moved. Verified with readelf, not assumed.
        if ! LDFLAGS='-Wl,-rpath,\$$ORIGIN:\$$ORIGIN/.. -Wl,--disable-new-dtags' \
             "$R/components/$c/configure" --prefix="$WEBOS_PREFIX" > cfg.log 2>&1 \
           || ! make -j"$(nproc)" > build.log 2>&1 || ! make install DESTDIR="$DESTDIR" > install.log 2>&1; then
            printf "%-22s FAILED %s\n" "$c" "$(grep -m1 -iE 'error' build.log cfg.log 2>/dev/null | cut -c1-60)"
            return 1
        fi
        printf "%-22s OK\n" "$c"
    done
}

stage_cmake() {
    echo "== CMake =="
    export PKG_CONFIG_PATH=$S/lib/pkgconfig:$S/usr/share/pkgconfig:$S/usr/lib/pkgconfig
    export LD_LIBRARY_PATH=$S/lib:$S/usr/lib
    local failed=0
    for c in $(selected cmake); do
        local d=$B/$c
        rm -rf "$d"; mkdir -p "$d"
        # Debug, as HP's desktop.pri configured its qmake builds (CONFIG +=
        # debug). Qt's own checks -- Q_ASSERT, and the receiver check on a
        # member-function connection -- only exist in a Debug build, and one of
        # them is what caught WebAppMgr's -fno-rtti crash.
        #
        # keyboard-efigs installs its plugins into the rootfs, not staging: they
        # are loaded at runtime by IMEManager from /usr/lib/luna, not linked
        # against.
        if ! cmake "$R/components/$c" -B "$d" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
             -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
             -DWEBOS_ROOTFS="$B/rootfs" \
             -DCMAKE_MODULE_PATH="$R/components/cmake-modules-webos" \
             -DWEBOS_INSTALL_ROOT="$WEBOS_PREFIX" -DCMAKE_INSTALL_PREFIX="$WEBOS_PREFIX" \
             -DCMAKE_INSTALL_RPATH='$ORIGIN:$ORIGIN/..' \
             -DCMAKE_EXE_LINKER_FLAGS='-Wl,--disable-new-dtags' \
             -DCMAKE_SHARED_LINKER_FLAGS='-Wl,--disable-new-dtags' > "$d/cfg.log" 2>&1; then
            printf "%-24s CONFIG FAILED %s\n" "$c" "$(grep -m1 -E 'Could NOT find|No package|CMake Error' "$d/cfg.log" | cut -c1-72)"
            failed=1; continue
        fi
        if ! make -C "$d" -j"$(nproc)" > "$d/build.log" 2>&1; then
            printf "%-24s BUILD FAILED  %s\n" "$c" "$(grep -m1 -E 'error:|undefined reference' "$d/build.log" | sed 's|.*/||' | cut -c1-72)"
            failed=1; continue
        fi
        if ! make -C "$d" install DESTDIR="$DESTDIR" > "$d/install.log" 2>&1; then
            printf "%-24s INSTALL FAILED %s\n" "$c" "$(grep -m1 -E 'cannot|Error' "$d/install.log" | cut -c1-60)"
            failed=1; continue
        fi
        # layout fixups the component does not do itself (HP did them by hand)
        [ -x "$R/tools/post-install/$c.sh" ] && "$R/tools/post-install/$c.sh" "$S"
        printf "%-24s OK\n" "$c"
    done
    return $failed
}

stage_node_addons() {
    echo "== node addons =="
    # HP's three addons, built from his sources against components/node-v8-shim.
    # Separate from the CMake stage because they are not one MANIFEST component:
    # one project builds all three, which is what lets them share the shim.
    # The node that ships, not whatever the host has: its headers are what the
    # addons compile against and its binary is what assemble-rootfs.sh puts in
    # the package. A missing one used to be skipped quietly, which built a tree
    # whose JavaScript services could never start; it is an error now.
    if ! . "$R/tools/node-home.sh"; then
        echo "  the pinned node ($(sed -n 's/^NODE_VERSION=//p' "$R/tools/node-version")) is not unpacked."
        echo "  Run tools/fetch-node.sh first -- the one step that needs the network."
        return 1
    fi
    echo "  node: $NODE_HOME"
    mkdir -p /tmp/webos
    # The rpath matters here and this stage never got one. The addons install to
    # <prefix>/usr/palm/nodejs and our libraries to <prefix>/usr/lib, so
    # $ORIGIN/../../lib reaches them from either tree. Without it they carry no
    # rpath at all -- and unlike our own executables, nothing else covers them:
    # they are loaded by node, which is not our binary and has no rpath of ours.
    # In the package that cost every JavaScript service, with
    # "Error: libluna-service2.so.3: cannot open shared object file", and with
    # them the profile account.
    # NODE_INCLUDE_DIR is given, not left to node-v8-shim to find. It finds the
    # node on PATH, but CMake caches the result, and this build directory is not
    # wiped between runs: MEASURED, with the pinned node first on PATH the addons
    # still compiled against the headers of the node cached from an earlier run.
    cmake -S "$R/components/node-v8-shim/addons" -B "$B/node-addons" \
          -DNODE_INCLUDE_DIR="$NODE_HOME/include/node" \
          -DCMAKE_INSTALL_PREFIX="$WEBOS_PREFIX" \
          -DCMAKE_INSTALL_RPATH='$ORIGIN/../../lib' \
          -DCMAKE_SHARED_LINKER_FLAGS='-Wl,--disable-new-dtags' > /tmp/webos/node-addons.log 2>&1 \
      && cmake --build "$B/node-addons" -j"$(nproc)" >> /tmp/webos/node-addons.log 2>&1 \
      && DESTDIR="$DESTDIR" cmake --install "$B/node-addons" >> /tmp/webos/node-addons.log 2>&1 \
      && echo "  pmloglib, palmbus, webos     OK" \
      || { echo "  FAILED (see /tmp/webos/node-addons.log)"; return 1; }
    # Ours: the bus for components/node-services, on Node-API directly.
    cmake -S "$R/components/node-services/native" -B "$B/node-services-native" \
          -DNODE_INCLUDE_DIR="$NODE_HOME/include/node" \
          -DCMAKE_INSTALL_PREFIX="$WEBOS_PREFIX" \
          -DCMAKE_INSTALL_RPATH='$ORIGIN/../../lib' \
          -DCMAKE_SHARED_LINKER_FLAGS='-Wl,--disable-new-dtags' > /tmp/webos/node-services-native.log 2>&1 \
      && cmake --build "$B/node-services-native" -j"$(nproc)" >> /tmp/webos/node-services-native.log 2>&1 \
      && DESTDIR="$DESTDIR" cmake --install "$B/node-services-native" >> /tmp/webos/node-services-native.log 2>&1 \
      && echo "  lunabus                      OK" \
      || { echo "  FAILED (see /tmp/webos/node-services-native.log)"; return 1; }
}

stage_powerd() {
    echo "== powerd =="
    # com.palm.power, ours rather than HP's: nothing in the CE drop provides it.
    # Not a MANIFEST component -- the MANIFEST stays an inventory of what HP
    # released -- so it gets its own stage, built against staging like the rest.
    export PKG_CONFIG_PATH=$S/lib/pkgconfig:$S/usr/share/pkgconfig:$S/usr/lib/pkgconfig
    mkdir -p /tmp/webos
    # -build in the name: the services stage already writes the running
    # service's output to /tmp/webos/sysfs-powerd.log, and the two used to
    # overwrite each other.
    # Installed to <prefix>/usr/sbin and copied to <rootfs>/usr/lib/luna, so the
    # libraries are one level up from either.
    cmake -S "$R/components/sysfs-powerd" -B "$B/sysfs-powerd" \
          -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
          -DCMAKE_INSTALL_PREFIX="$WEBOS_PREFIX" \
          -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib:$ORIGIN/..' \
          -DCMAKE_EXE_LINKER_FLAGS='-Wl,--disable-new-dtags' > /tmp/webos/sysfs-powerd-build.log 2>&1 \
      && cmake --build "$B/sysfs-powerd" -j"$(nproc)" >> /tmp/webos/sysfs-powerd-build.log 2>&1 \
      && DESTDIR="$DESTDIR" cmake --install "$B/sysfs-powerd" >> /tmp/webos/sysfs-powerd-build.log 2>&1 \
      && echo "  sysfs-powerd                 OK" \
      || { echo "  FAILED (see /tmp/webos/sysfs-powerd-build.log)"; return 1; }
}

stage_connmgr() {
    echo "== connmgr =="
    # com.palm.connectionmanager from NetworkManager, ours rather than HP's:
    # the CE drop answers this call with pmnetconfigmanager-stub, a constant
    # that says "connected, over wifi, always". Not a MANIFEST component -- the
    # MANIFEST stays an inventory of what HP released -- so it gets its own
    # stage, built against staging like sysfs-powerd.
    export PKG_CONFIG_PATH=$S/lib/pkgconfig:$S/usr/share/pkgconfig:$S/usr/lib/pkgconfig
    mkdir -p /tmp/webos
    # -build in the name, as with sysfs-powerd: the services stage writes the
    # running service's own output to /tmp/webos/nm-connectionmanager.log.
    cmake -S "$R/components/nm-connectionmanager" -B "$B/nm-connectionmanager" \
          -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
          -DCMAKE_INSTALL_PREFIX="$WEBOS_PREFIX" \
          -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib:$ORIGIN/..' \
          -DCMAKE_EXE_LINKER_FLAGS='-Wl,--disable-new-dtags' > /tmp/webos/nm-connectionmanager-build.log 2>&1 \
      && cmake --build "$B/nm-connectionmanager" -j"$(nproc)" >> /tmp/webos/nm-connectionmanager-build.log 2>&1 \
      && DESTDIR="$DESTDIR" cmake --install "$B/nm-connectionmanager" >> /tmp/webos/nm-connectionmanager-build.log 2>&1 \
      && echo "  nm-connectionmanager         OK" \
      || { echo "  FAILED (see /tmp/webos/nm-connectionmanager-build.log)"; return 1; }
}

stage_storaged() {
    echo "== storaged =="
    # com.palm.storage, ours rather than HP's: nothing in the CE drop answers
    # that name, and Open webOS's storaged is written against a phone's USB
    # gadget. Not a MANIFEST component either, so it gets its own stage.
    export PKG_CONFIG_PATH=$S/lib/pkgconfig:$S/usr/share/pkgconfig:$S/usr/lib/pkgconfig
    mkdir -p /tmp/webos
    cmake -S "$R/components/storaged" -B "$B/storaged" \
          -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
          -DCMAKE_INSTALL_PREFIX="$WEBOS_PREFIX" \
          -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib:$ORIGIN/..' \
          -DCMAKE_EXE_LINKER_FLAGS='-Wl,--disable-new-dtags' > /tmp/webos/storaged-build.log 2>&1 \
      && cmake --build "$B/storaged" -j"$(nproc)" >> /tmp/webos/storaged-build.log 2>&1 \
      && DESTDIR="$DESTDIR" cmake --install "$B/storaged" >> /tmp/webos/storaged-build.log 2>&1 \
      && echo "  storaged                     OK" \
      || { echo "  FAILED (see /tmp/webos/storaged-build.log)"; return 1; }
}

stage_rootfs() {
    echo "== rootfs =="
    # The MANIFEST's "copiar" components are not built: they are JS, themes and
    # data. assemble-rootfs.sh places them next to the installed binaries.
    # WEBOS_STAGING, or this looks for the staging tree under build/ while a
    # DESTDIR build has just put it somewhere else entirely.
    WEBOS_STAGING="$S" "$R/tools/assemble-rootfs.sh"
}

case "$STAGE" in
    headers) stage_headers ;;
    autotools) stage_autotools ;;
    cmake)  stage_cmake ;;
    node)   stage_node_addons ;;
    powerd) stage_powerd ;;
    connmgr) stage_connmgr ;;
    storaged) stage_storaged ;;
    rootfs) stage_rootfs ;;
    all)   # NOTE the placement: the echoes go INSIDE the if, not loose after
            # the chain. They were outside and the script announced success even
            # when a stage had failed.
            if stage_headers && stage_autotools \
               && stage_cmake && stage_node_addons && stage_powerd \
               && stage_connmgr && stage_storaged && stage_rootfs; then
                echo
                echo "Done. To start the shell:  tools/run-lunasysmgr.sh"
            else
                echo
                echo "FAILED: a stage did not finish. See the logs in build/." >&2
                exit 1
            fi ;;
    *)      echo "unknown stage: $STAGE (headers | autotools | cmake | node | powerd | connmgr | rootfs | all)"; exit 2 ;;
esac
