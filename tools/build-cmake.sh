#!/bin/bash
# Sweep: tries to configure and build each CMake component on modern Debian.
# It fixes nothing; it only maps what breaks and where.
R="$(cd "$(dirname "$0")/.." && pwd)"
S=$R/build-modern/staging
export PKG_CONFIG_PATH=$S/lib/pkgconfig:$S/usr/share/pkgconfig:$S/usr/lib/pkgconfig
export LD_LIBRARY_PATH=$S/lib:$S/usr/lib

for c in "$@"; do
    d=$R/build-modern/$c
    rm -rf "$d"; mkdir -p "$d"; cd "$d" || continue
    if ! cmake $R/components/$c -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
         -DCMAKE_MODULE_PATH="$R/components/cmake-modules-webos" \
         -DWEBOS_INSTALL_ROOT=$S -DCMAKE_INSTALL_PREFIX=$S > cfg.log 2>&1; then
        motivo=$(grep -m1 -E "Could NOT find|No package|CMake Error" cfg.log | cut -c1-72)
        printf "%-24s CONFIG FALLA  %s\n" "$c" "$motivo"
        continue
    fi
    if ! make -j6 > build.log 2>&1; then
        motivo=$(grep -m1 -E "error:|undefined reference" build.log | sed 's|.*/||' | cut -c1-72)
        printf "%-24s COMPILA FALLA %s\n" "$c" "$motivo"
        continue
    fi
    if ! make install > install.log 2>&1; then
        printf "%-24s INSTALA FALLA %s\n" "$c" "$(grep -m1 -E 'cannot|Error' install.log | cut -c1-60)"
        continue
    fi
    # layout fixups the component does not do itself (HP did them by hand)
    [ -x "$R/tools/post-install/$c.sh" ] && "$R/tools/post-install/$c.sh" "$S"
    printf "%-24s OK\n" "$c"
done
