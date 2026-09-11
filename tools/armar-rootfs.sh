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

# --- WebAppMgr: el proceso que corre las apps web ---
# No se lanza a mano. Es un servicio de LS2: LunaSysMgr le habla por el bus
# (WebAppMgrProxy) y ls-hubd lo arranca solo, leyendo el Exec= de estos .service.
WAM="$R/components/webappmanager"
if [ -x "$WAM/debug-x86/WebAppMgr" ]; then
    cp -f "$WAM"/debug-x86/WebAppMgr "$ROOTFS/usr/lib/luna/WebAppMgr"
    cp -f "$WAM"/desktop-support/com.palm.webappmgr.json.prv    "$ROOTFS/usr/share/ls2/roles/prv/com.palm.webappmgr.json"
    cp -f "$WAM"/desktop-support/com.palm.webappmgr.json.pub    "$ROOTFS/usr/share/ls2/roles/pub/com.palm.webappmgr.json"
    cp -f "$WAM"/desktop-support/com.palm.webappmgr.service.prv "$ROOTFS/usr/share/ls2/system-services/com.palm.webappmgr.service"
    cp -f "$WAM"/desktop-support/com.palm.webappmgr.service.pub "$ROOTFS/usr/share/ls2/services/com.palm.webappmgr.service"
fi

# Los .service traen "Exec=/usr/lib/luna/..." absoluto, que en el dispositivo era
# correcto. Aqui ls-hubd corre FUERA del bwrap (solo LunaSysMgr entra), asi que
# resolveria esa ruta contra el sistema de verdad, donde no hay nada. Se
# reapunta al rootfs. HP resolvia lo mismo con symlinks desde /usr/lib/luna,
# pero eso exige root y toca el sistema.
sed -i "s|^Exec=/usr/lib/luna/|Exec=$ROOTFS/usr/lib/luna/|" \
    "$ROOTFS"/usr/share/ls2/services/*.service \
    "$ROOTFS"/usr/share/ls2/system-services/*.service 2>/dev/null

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


# --- Contenido: apps, frameworks y servicios (componentes que solo se copian) ---
mkdir -p "$ROOTFS"/usr/palm/{applications,services,frameworks} "$ROOTFS"/etc/palm/db/{kinds,permissions}

# Enyo: el framework sobre el que estan escritas todas las apps
mkdir -p "$ROOTFS/usr/palm/frameworks/enyo/0.10/framework"
cp -rf "$C"/enyo-1.0/framework/* "$ROOTFS/usr/palm/frameworks/enyo/0.10/framework/" 2>/dev/null
ln -sfn 0.10 "$ROOTFS/usr/palm/frameworks/enyo/version" 2>/dev/null

# Las apps de HP. Cada una lleva sus 'kinds' y permisos de db8.
for APP in "$C"/core-apps/*/; do
    [ -f "$APP/appinfo.json" ] || continue
    cp -rf "$APP" "$ROOTFS/usr/palm/applications/" 2>/dev/null
    cp -rf "$APP"/configuration/db/kinds/*       "$ROOTFS/etc/palm/db/kinds/"       2>/dev/null
    cp -rf "$APP"/configuration/db/permissions/* "$ROOTFS/etc/palm/db/permissions/" 2>/dev/null
done

# Servicios de aplicacion (JS, corren sobre node)
for SVC in "$C"/app-services/*/; do
    [ -f "$SVC/services.json" ] || [ -f "$SVC/package.json" ] || continue
    cp -rf "$SVC" "$ROOTFS/usr/palm/services/" 2>/dev/null
    cp -rf "$SVC"/db/kinds/*       "$ROOTFS/etc/palm/db/kinds/"       2>/dev/null
    cp -rf "$SVC"/db/permissions/* "$ROOTFS/etc/palm/db/permissions/" 2>/dev/null
done

# Frameworks: cada uno bajo <nombre>/version/1.0/
for GRUPO in foundation-frameworks mojoservice-frameworks loadable-frameworks; do
    for FW in "$C"/$GRUPO/*/; do
        n=$(basename "$FW"); [ "$n" = "." ] && continue
        case "$n" in .git|*.md|files) continue;; esac
        mkdir -p "$ROOTFS/usr/palm/frameworks/$n/version/1.0"
        cp -rf "$FW"/* "$ROOTFS/usr/palm/frameworks/$n/version/1.0/" 2>/dev/null
    done
done
mkdir -p "$ROOTFS/usr/palm/frameworks/underscore/version/1.0"
cp -rf "$C"/underscore/* "$ROOTFS/usr/palm/frameworks/underscore/version/1.0/" 2>/dev/null
cp -f "$C"/mojoloader/mojoloader.js "$ROOTFS/usr/palm/frameworks/" 2>/dev/null

echo "  apps:                $(ls "$ROOTFS/usr/palm/applications" 2>/dev/null | wc -l)"
echo "  servicios:           $(ls "$ROOTFS/usr/palm/services" 2>/dev/null | wc -l)"
echo "  frameworks:          $(ls "$ROOTFS/usr/palm/frameworks" 2>/dev/null | wc -l)"
echo "  kinds de db8:        $(ls "$ROOTFS/etc/palm/db/kinds" 2>/dev/null | wc -l)"

# --- reapuntar las rutas de luna.conf al rootfs ---
# Son absolutas del dispositivo (/usr/palm/..., /var/luna/...). Con bwrap solo
# se puede montar /etc/palm, porque es la unica ruta clavada en el codigo
# (Settings.cpp); las demas no se pueden montar encima porque /usr y /var son
# de solo lectura dentro del namespace y bwrap no puede crear ahi el punto de
# montaje. Pero no hace falta: luna.conf existe precisamente para configurarlas.
# Sin esto LunaSysMgr dibuja el shell pero no encuentra NINGUNA app.
mkdir -p "$ROOTFS"/usr/lib/luna/applications "$ROOTFS"/usr/palm/sysmgr/{images,localization} \
         "$ROOTFS"/var/luna/{launchpoints,preferences}
sed -i -E "/^(ApplicationPath|SystemPath|SystemResourcesPath|SystemLocalePath|AppLauncherPath|LaunchPointsPath|PreferencesPath)=/ s#(=|:)/#\\1$ROOTFS/#g" \
    "$ROOTFS/etc/palm/luna.conf"

# Lo mismo para el bus: los .conf de ls2 apuntan a /usr/share/ls2/... y ls-hubd
# no encontraba ni los roles ni los servicios, asi que LunaSysMgr no llegaba a
# registrar com.palm.applicationManager y nada se podia lanzar.
sed -i -E "s#^(Directories=)/#\\1$ROOTFS/#" "$ROOTFS"/etc/ls2/*.conf

echo "rootfs armado en $ROOTFS"
echo "  etc/palm:            $(ls "$ROOTFS/etc/palm" 2>/dev/null | wc -l) entradas"
echo "  etc/ls2:             $(ls "$ROOTFS/etc/ls2" 2>/dev/null | wc -l) entradas"
echo "  luna-systemui:       $(ls "$ROOTFS/usr/lib/luna/system/luna-systemui" 2>/dev/null | wc -l) entradas"
echo "  luna-applauncher:    $(ls "$ROOTFS/usr/lib/luna/system/luna-applauncher" 2>/dev/null | wc -l) entradas"
echo "  roles del bus:       $(ls "$ROOTFS/usr/share/ls2/roles/prv" "$ROOTFS/usr/share/ls2/roles/pub" 2>/dev/null | grep -c json)"
