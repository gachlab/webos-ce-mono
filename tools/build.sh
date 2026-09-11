#!/bin/bash
# Builds all of webOS on a modern Debian, in MANIFEST order.
#
# The order is not ours: it is HP's build-webos-desktop.sh, which already came
# topologically sorted. MANIFEST.tsv keeps it in its first column.
#
# Usage:
#   tools/build.sh              # everything
#   tools/build.sh cmake        # one stage: third-party | headers | autotools |
#                               # cmake | qmake | rootfs
set -u
R="$(cd "$(dirname "$0")/.." && pwd)"
STAGE="${1:-all}"
S="$R/build-modern/staging"

# Components the MANIFEST lists as cmake/qmake that we do NOT build, and why.
# They are listed here instead of being removed from the MANIFEST so that the
# manifest stays a faithful inventory of what HP released.
declare -A SKIP=(
    [cmake]="it is the tool itself; we use the system one"
    [cmake-modules-webos]="CMake modules, consumed via CMAKE_MODULE_PATH"
    [qt4]="replaced by the system Qt5"
    [webkit]="replaced by QtWebKit 5.212; built by build-third-party.sh"
    [nodejs]="we use Debian node; HP's needs Python 2 and SCons"
    [nodejs-module-webos-sysbus]="old v8 API addon, needs porting to N-API"
    [nodejs-module-webos-pmlog]="same"
    [nodejs-module-webos-dynaload]="same"
    [WebKitSupplemental]="browser path (NPAPI), pending"
    [AdapterBase]="browser path (NPAPI), pending"
    [BrowserServer]="browser path (NPAPI), pending"
    [BrowserAdapter]="browser path (NPAPI), pending"
    # NOTE: db8 configures and builds without leveldb, and Debian does not
    # package it. Still to confirm at runtime whether mojodb-luna needs that
    # backend or ships another.
    [leveldb]="db8 builds without it; still to verify at runtime"
)

list_of() {  # list_of <build-system> -> names in MANIFEST order
    # The four components the MANIFEST marks as qmake now have a CMakeLists.txt
    # of their own, so they are built with everything else. Their position in the
    # MANIFEST already puts them after what they depend on.
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
        # and 'common' has to go first because the other three link against
        # it.
        if [ "$c" = mojomail ]; then
            echo mojomail/common mojomail/imap mojomail/pop mojomail/smtp
            continue
        fi
        echo "$c"
    done
}

stage_third_party() {
    echo "== third-party =="
    # QtWebKit 5.212. The only thing that does not live in the repo, and it
    # takes a while, so it is skipped when already installed.
    if [ -e "$R/build-modern/staging/qtwebkit/mkspecs/modules/qt_lib_webkit.pri" ]; then
        echo "QtWebKit                already installed"
    else
        "$R/tools/build-third-party.sh"
    fi
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

    cp -f "$R"/components/npapi-headers/*.h "$S/include/webkit/npapi/" 2>/dev/null
    printf "  %-22s %s\n" "npapi-headers" "$(ls "$S/include/webkit/npapi" | wc -l) headers"
}

stage_autotools() {
    echo "== autotools =="
    for c in $(selected autotools); do
        d=$R/build-modern/$c
        mkdir -p "$d"; cd "$d" || { echo "$c: sin directorio"; return 1; }
        # cjson ships autogen.sh; HP's tree has no generated ./configure.
        [ -x "$R/components/$c/configure" ] || (cd "$R/components/$c" && ./autogen.sh >/dev/null 2>&1)
        if ! "$R/components/$c/configure" --prefix="$R/build-modern/staging" > cfg.log 2>&1 \
           || ! make -j"$(nproc)" > build.log 2>&1 || ! make install > install.log 2>&1; then
            printf "%-22s FALLA %s\n" "$c" "$(grep -m1 -iE 'error' build.log cfg.log 2>/dev/null | cut -c1-60)"
            return 1
        fi
        printf "%-22s OK\n" "$c"
    done
}

stage_cmake() {
    echo "== CMake =="
    # shellcheck disable=SC2046
    "$R/tools/build-cmake.sh" $(selected cmake)
}

stage_qmake() {
    # Kept so "build.sh qmake" still works: HP's .pro files are still in the tree
    # and remain the reference the CMake translation was verified against.
    # The normal path no longer needs it -- the cmake stage covers those four.
    echo "== qmake (HP's original build, no longer part of 'all') =="
    "$R/tools/build-qmake.sh"
}

stage_rootfs() {
    echo "== rootfs =="
    # The MANIFEST's "copiar" components are not built: they are JS, themes and
    # data. assemble-rootfs.sh places them next to the installed binaries.
    "$R/tools/assemble-rootfs.sh"
}

case "$STAGE" in
    third-party) stage_third_party ;;
    headers) stage_headers ;;
    autotools) stage_autotools ;;
    cmake)  stage_cmake ;;
    qmake)  stage_qmake ;;
    rootfs) stage_rootfs ;;
    all)   # NOTE the placement: the echoes go INSIDE the if, not loose after
            # the chain. They were outside and the script announced success even
            # when a stage had failed.
            if stage_third_party && stage_headers && stage_autotools \
               && stage_cmake && stage_rootfs; then
                echo
                echo "Done. To start the shell:  tools/run-lunasysmgr.sh"
            else
                echo
                echo "FAILED: a stage did not finish. See the logs in build-modern/." >&2
                exit 1
            fi ;;
    *)      echo "unknown stage: $STAGE (third-party | headers | autotools | cmake | qmake | rootfs | all)"; exit 2 ;;
esac
