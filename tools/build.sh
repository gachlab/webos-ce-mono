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
#                               # node | rootfs
set -u
R="$(cd "$(dirname "$0")/.." && pwd)"
STAGE="${1:-all}"
B="$R/build"
S="$B/staging"
BUILD_TYPE="${BUILD_TYPE:-Debug}"

# Components the MANIFEST lists as cmake/qmake that we do NOT build, and why.
# They are listed here instead of being removed from the MANIFEST so that the
# manifest stays a faithful inventory of what HP released.
declare -A SKIP=(
    [cmake]="it is the tool itself; we use the system one"
    [cmake-modules-webos]="CMake modules, consumed via CMAKE_MODULE_PATH"
    [qt4]="replaced by Debian's Qt 6"
    [webkit]="replaced by QtWebEngine; WebAppMgr reaches it through components/qtwebkit-compat"
    [nodejs]="we use Debian node; HP's needs Python 2 and SCons"
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
    if [ "$1" = cmake ]; then
        awk -F'\t' 'NR>1 && ($5=="cmake" || $5=="qmake") {print $2}' "$R/MANIFEST.tsv"
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
        mkdir -p "$d"; cd "$d" || { echo "$c: no directory"; return 1; }
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
        if ! "$R/components/$c/configure" --prefix="$S" > cfg.log 2>&1 \
           || ! make -j"$(nproc)" > build.log 2>&1 || ! make install > install.log 2>&1; then
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
             -DWEBOS_INSTALL_ROOT="$S" -DCMAKE_INSTALL_PREFIX="$S" > "$d/cfg.log" 2>&1; then
            printf "%-24s CONFIG FAILED %s\n" "$c" "$(grep -m1 -E 'Could NOT find|No package|CMake Error' "$d/cfg.log" | cut -c1-72)"
            failed=1; continue
        fi
        if ! make -C "$d" -j"$(nproc)" > "$d/build.log" 2>&1; then
            printf "%-24s BUILD FAILED  %s\n" "$c" "$(grep -m1 -E 'error:|undefined reference' "$d/build.log" | sed 's|.*/||' | cut -c1-72)"
            failed=1; continue
        fi
        if ! make -C "$d" install > "$d/install.log" 2>&1; then
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
    if ! command -v node >/dev/null; then
        echo "  no node on PATH; skipped"
        return 0
    fi
    mkdir -p /tmp/webos
    cmake -S "$R/components/node-v8-shim/addons" -B "$B/node-addons" \
          -DCMAKE_INSTALL_PREFIX="$S" > /tmp/webos/node-addons.log 2>&1 \
      && cmake --build "$B/node-addons" -j"$(nproc)" >> /tmp/webos/node-addons.log 2>&1 \
      && cmake --install "$B/node-addons" >> /tmp/webos/node-addons.log 2>&1 \
      && echo "  pmloglib, palmbus, webos     OK" \
      || { echo "  FAILED (see /tmp/webos/node-addons.log)"; return 1; }
}

stage_rootfs() {
    echo "== rootfs =="
    # The MANIFEST's "copiar" components are not built: they are JS, themes and
    # data. assemble-rootfs.sh places them next to the installed binaries.
    "$R/tools/assemble-rootfs.sh"
}

case "$STAGE" in
    headers) stage_headers ;;
    autotools) stage_autotools ;;
    cmake)  stage_cmake ;;
    node)   stage_node_addons ;;
    rootfs) stage_rootfs ;;
    all)   # NOTE the placement: the echoes go INSIDE the if, not loose after
            # the chain. They were outside and the script announced success even
            # when a stage had failed.
            if stage_headers && stage_autotools \
               && stage_cmake && stage_node_addons && stage_rootfs; then
                echo
                echo "Done. To start the shell:  tools/run-lunasysmgr.sh"
            else
                echo
                echo "FAILED: a stage did not finish. See the logs in build/." >&2
                exit 1
            fi ;;
    *)      echo "unknown stage: $STAGE (headers | autotools | cmake | node | rootfs | all)"; exit 2 ;;
esac
