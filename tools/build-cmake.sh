#!/bin/bash
# Sweep: tries to configure and build each CMake component on modern Debian.
# It fixes nothing; it only maps what breaks and where.
R="$(cd "$(dirname "$0")/.." && pwd)"
# WEBOS_QT=6 builds into its own tree, build-qt6/, so the Qt 5 build that runs
# today is left untouched.
WEBOS_QT="${WEBOS_QT:-5}"
if [ "$WEBOS_QT" = 5 ]; then B=$R/build-modern; else B=$R/build-qt$WEBOS_QT; fi
S=$B/staging
# A Qt 6 tree starts from a copy of the Qt 5 staging: everything in it that does
# not use Qt (pbnjson, luna-service2, ...) is the same either way. A copy, not
# hardlinks: an install writing over a hardlinked file would change the Qt 5
# tree too.
if [ "$WEBOS_QT" != 5 ] && [ ! -d "$S" ]; then
    mkdir -p "$B"
    cp -a "$R/build-modern/staging" "$S"
    rm -rf "$S/qtwebkit"
fi
export PKG_CONFIG_PATH=$S/lib/pkgconfig:$S/usr/share/pkgconfig:$S/usr/lib/pkgconfig
export LD_LIBRARY_PATH=$S/lib:$S/usr/lib

for c in "$@"; do
    d=$B/$c
    rm -rf "$d"; mkdir -p "$d"; cd "$d" || continue
    # Debug so the flags match what qmake's desktop.pri used (CONFIG += debug).
    # keyboard-efigs installs its plugins into the rootfs, not staging: they are
    # loaded at runtime by IMEManager from /usr/lib/luna, not linked against.
    if ! cmake $R/components/$c -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
         -DCMAKE_BUILD_TYPE=${BUILD_TYPE:-Debug} \
         -DWEBOS_ROOTFS=$B/rootfs -DWEBOS_QT_MAJOR=$WEBOS_QT \
         -DCMAKE_MODULE_PATH="$R/components/cmake-modules-webos" \
         -DWEBOS_INSTALL_ROOT=$S -DCMAKE_INSTALL_PREFIX=$S > cfg.log 2>&1; then
        reason=$(grep -m1 -E "Could NOT find|No package|CMake Error" cfg.log | cut -c1-72)
        printf "%-24s CONFIG FAILED %s\n" "$c" "$reason"
        continue
    fi
    if ! make -j6 > build.log 2>&1; then
        reason=$(grep -m1 -E "error:|undefined reference" build.log | sed 's|.*/||' | cut -c1-72)
        printf "%-24s BUILD FAILED  %s\n" "$c" "$reason"
        continue
    fi
    if ! make install > install.log 2>&1; then
        printf "%-24s INSTALL FAILED %s\n" "$c" "$(grep -m1 -E 'cannot|Error' install.log | cut -c1-60)"
        continue
    fi
    # layout fixups the component does not do itself (HP did them by hand)
    [ -x "$R/tools/post-install/$c.sh" ] && "$R/tools/post-install/$c.sh" "$S"
    printf "%-24s OK\n" "$c"
done
