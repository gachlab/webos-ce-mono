#!/bin/bash
# Arma el arbol de ejecucion que LunaSysMgr espera, replicando las operaciones
# que build-webos-desktop.sh hacia sobre $ROOTFS. Sin esto el binario arranca
# pero dibuja una ventana vacia: no encuentra ni configuracion ni recursos de UI.
set -u
R="$(cd "$(dirname "$0")/.." && pwd)"
C="$R/components"
ROOTFS="${1:-$R/build-modern/rootfs}"
S="$R/build-modern/staging"

mkdir -p "$ROOTFS"/{etc/palm/pubsub_handlers,etc/ls2,usr/lib/luna/system,usr/palm/sounds}
mkdir -p "$ROOTFS"/usr/share/ls2/{roles/prv,roles/pub,services,system-services}
mkdir -p "$ROOTFS"/usr/lib/luna/customization "$ROOTFS"/var/{db,luna,palm} "$ROOTFS"/usr/share/fonts

# --- el bus ---
cp -f "$R"/desktop-support/ls2/*.conf "$ROOTFS/etc/ls2/"

# --- LunaSysMgr: configuracion, roles del bus, sonidos ---
LS="$C/luna-sysmgr"
cp -f "$LS"/conf/luna.conf                        "$ROOTFS/etc/palm/"
cp -f "$LS"/conf/luna-desktop.conf                "$ROOTFS/etc/palm/luna-platform.conf"
cp -f "$LS"/conf/lunaAnimations.conf              "$ROOTFS/etc/palm/"
cp -f "$LS"/conf/notificationPolicy.conf          "$ROOTFS/etc/palm/"
cp -f "$LS"/conf/defaultPreferences.txt           "$ROOTFS/etc/palm/"
cp -f "$LS"/conf/default-exhibition-apps.json     "$ROOTFS/etc/palm/"
cp -f "$LS"/conf/default-launcher-page-layout.json "$ROOTFS/etc/palm/"
cp -f "$LS"/conf/default-exhibition-apps.json     "$ROOTFS/usr/lib/luna/customization/"
cp -f "$LS"/service/com.palm.appinstaller.pubsub  "$ROOTFS/etc/palm/pubsub_handlers/com.palm.appinstaller"
cp -f "$LS"/desktop-support/com.palm.luna.json.prv    "$ROOTFS/usr/share/ls2/roles/prv/com.palm.luna.json"
cp -f "$LS"/desktop-support/com.palm.luna.json.pub    "$ROOTFS/usr/share/ls2/roles/pub/com.palm.luna.json"
cp -f "$LS"/desktop-support/com.palm.luna.service.prv "$ROOTFS/usr/share/ls2/system-services/com.palm.luna.service"
cp -f "$LS"/desktop-support/com.palm.luna.service.pub "$ROOTFS/usr/share/ls2/services/com.palm.luna.service"
cp -rf "$LS"/sounds/* "$ROOTFS/usr/palm/sounds/" 2>/dev/null
mkdir -p "$ROOTFS/usr/lib/luna"
cp -f "$LS"/debug-x86/LunaSysMgr "$ROOTFS/usr/lib/luna/LunaSysMgr"

# --- la UI: estas dos son las que realmente dibujan ---
mkdir -p "$ROOTFS/usr/lib/luna/system/luna-systemui"
cp -rf "$C"/luna-systemui/* "$ROOTFS/usr/lib/luna/system/luna-systemui/" 2>/dev/null
mkdir -p "$ROOTFS/usr/lib/luna/system/luna-applauncher"
cp -rf "$C"/luna-applauncher/* "$ROOTFS/usr/lib/luna/system/luna-applauncher/" 2>/dev/null
cp -f "$LS"/desktop-support/appinfo.json "$ROOTFS/usr/lib/luna/system/luna-applauncher/appinfo.json"

# --- configuracion base y fuentes ---
cp -f "$C"/luna-init/files/conf/*.json "$ROOTFS/usr/palm/" 2>/dev/null
cp -f "$C"/luna-init/files/conf/fonts/*.xml "$ROOTFS/usr/share/fonts/" 2>/dev/null
cp -rf "$C"/isis-fonts/* "$ROOTFS/usr/share/fonts/" 2>/dev/null

echo "rootfs armado en $ROOTFS"
echo "  etc/palm:            $(ls "$ROOTFS/etc/palm" 2>/dev/null | wc -l) entradas"
echo "  etc/ls2:             $(ls "$ROOTFS/etc/ls2" 2>/dev/null | wc -l) entradas"
echo "  luna-systemui:       $(ls "$ROOTFS/usr/lib/luna/system/luna-systemui" 2>/dev/null | wc -l) entradas"
echo "  luna-applauncher:    $(ls "$ROOTFS/usr/lib/luna/system/luna-applauncher" 2>/dev/null | wc -l) entradas"
echo "  roles del bus:       $(ls "$ROOTFS/usr/share/ls2/roles/prv" "$ROOTFS/usr/share/ls2/roles/pub" 2>/dev/null | grep -c json)"
