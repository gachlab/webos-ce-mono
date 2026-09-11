#!/bin/bash
# Builds webOS's qmake components against the system Qt5.
#
# Companion to build-cmake.sh, which only knows about CMake. HP's qmake
# projects need three things that are not obvious:
#
#  1. -after on qmake. HP's .pro files ASSIGN INCLUDEPATH ("INCLUDEPATH =
#     $$VPATH"), they do not extend it. An "INCLUDEPATH +=" passed before is
#     lost when the .pro reassigns; -after applies it at the end.
#  2. make -j1. The .pro files declare their dependencies badly and the build
#     is NOT parallel-safe: you get a truncated .o, and make will not rebuild
#     it because its timestamp is already newer than the source. It has to be
#     deleted by hand.
#  3. LUNA_STAGING and ROOTFS. HP's .pri files read them from the environment.
#     LUNA_STAGING builds the -L and -rpath flags; ROOTFS is keyboard-efigs's
#     install destination, which does not go to staging but alongside the
#     runtime data. Without exporting it, its "$$(ROOTFS)/usr/lib/luna"
#     resolves to /usr/lib/luna.
#
# Usage: tools/build-qmake.sh [component...]   (no arguments: all of them)
set -u

R="$(cd "$(dirname "$0")/.." && pwd)"
S=$R/build-modern/staging

export LUNA_STAGING=$S
export ROOTFS=${ROOTFS:-$R/build-modern/rootfs}
export PKG_CONFIG_PATH=$S/lib/pkgconfig:$S/usr/lib/pkgconfig:$S/usr/share/pkgconfig
export LD_LIBRARY_PATH=$S/lib:$S/usr/lib

QMAKE=${QMAKE:-/usr/lib/qt5/bin/qmake}

# QtWebKit 5.212 does not ship with Debian; we build it separately (see
# patches/). Its module .pri files live in their own mkspecs, and qmake only
# finds them through QMAKEPATH. Without this, "QT += webkit webkitwidgets"
# fails with "Unknown module".
QTWEBKIT=${QTWEBKIT:-$S/qtwebkit}
[ -d "$QTWEBKIT/mkspecs/modules" ] && export QMAKEPATH="$QTWEBKIT"

# The order is the MANIFEST's, which in turn is HP's script's.
ORDEN="luna-sysmgr-common luna-sysmgr keyboard-efigs webappmanager"
declare -A PRO=(
    [luna-sysmgr-common]=sysmgr-common.pro
    [luna-sysmgr]=sysmgr.pro
    [keyboard-efigs]=keyboard-efigs.pro
    [webappmanager]=webappmgr.pro
)

INCS="$S/include $S/usr/include $S/usr/include/sysmgr-ipc \
      $S/include/luna-sysmgr-common $S/include/luna-service2 \
      $S/include/ime $S/include/webkit/npapi"

failed=0
for c in ${@:-$ORDEN}; do
    d=$R/components/$c
    p=${PRO[$c]:-}
    if [ -z "$p" ]; then echo "$c: not a known qmake component"; failed=1; continue; fi

    cd "$d" || { failed=1; continue; }
    # Incremental by default. The wipe existed because of the truncated .o from
    # parallel runs, but since -j1 is forced here that race cannot happen;
    # always wiping meant recompiling 193,000 lines to change one. CLEAN=1
    # forces the wipe.
    # qmake is also re-run when the previous Makefile came from a qmake that
    # emitted warnings. A "Failure to find" means a header listed in HEADERS did
    # not exist then, and qmake leaves it with NO moc rule: the Makefile stays
    # silent forever even if the header shows up later. That is how
    # IMEDataInterface was lost, and linking failed with qt_metacall undefined.
    if [ -f qmake.log ] && grep -q "Failure to find" qmake.log; then
        rm -f Makefile.Ubuntu*
    fi

    if [ -n "${CLEAN:-}" ] || [ ! -f Makefile.Ubuntu ]; then
        rm -rf debug-x86 release-x86 .qmake.stash Makefile.Ubuntu*
        hacer_qmake=1
    else
        hacer_qmake=
    fi

    if [ -n "$hacer_qmake" ]; then
        if ! "$QMAKE" "$p" -after \
                "INCLUDEPATH += $INCS" \
                "LIBS += -L$S/lib -L$S/usr/lib" > qmake.log 2>&1; then
            printf "%-22s QMAKE FAILED  %s\n" "$c" "$(grep -m1 -iE 'error|cannot' qmake.log | cut -c1-60)"
            failed=1; continue
        fi
    fi

    if ! make -f Makefile.Ubuntu -j1 > build.log 2>&1; then
        printf "%-22s BUILD FAILED  %s\n" "$c" \
            "$(grep -m1 -E 'error:|undefined reference' build.log | sed 's|.*/||' | cut -c1-60)"
        failed=1; continue
    fi

    if ! make -f Makefile.Ubuntu install > install.log 2>&1; then
        printf "%-22s INSTALL FAILED %s\n" "$c" "$(grep -m1 -iE 'cannot|error' install.log | cut -c1-60)"
        failed=1; continue
    fi

    printf "%-22s OK\n" "$c"
done
exit $failed
