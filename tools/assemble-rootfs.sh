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


# --- la UI: estas dos son las que realmente dibujan ---
mkdir -p "$ROOTFS/usr/lib/luna/system/luna-systemui"
cp -rf "$C"/luna-systemui/* "$ROOTFS/usr/lib/luna/system/luna-systemui/" 2>/dev/null
# El fondo de pantalla va dentro de un tar, no suelto (igual que las fuentes
# Prelude en fonts.tgz). Sin extraerlo el lock screen sale negro: dentro esta
# bluerocks.png, que es el fondo por defecto de webOS.
tar xf "$C"/luna-systemui/images/wallpaper.tar \
    -C "$ROOTFS/usr/lib/luna/system/luna-systemui/images" 2>/dev/null
mkdir -p "$ROOTFS/usr/lib/luna/system/luna-applauncher"
cp -rf "$C"/luna-applauncher/* "$ROOTFS/usr/lib/luna/system/luna-applauncher/" 2>/dev/null
cp -f "$LS"/desktop-support/appinfo.json "$ROOTFS/usr/lib/luna/system/luna-applauncher/appinfo.json"

# --- configuracion base y fuentes ---
cp -f "$C"/luna-init/files/conf/*.json "$ROOTFS/usr/palm/" 2>/dev/null
cp -f "$C"/luna-init/files/conf/fonts/*.xml "$ROOTFS/usr/share/fonts/" 2>/dev/null
cp -rf "$C"/isis-fonts/* "$ROOTFS/usr/share/fonts/" 2>/dev/null
# Las Prelude van dentro de un tarball, no sueltas. Son LA tipografia de webOS:
# sin ellas el reloj grande de la pantalla de bloqueo no se dibuja.
tar xzf "$C"/luna-init/files/conf/fonts/fonts.tgz -C "$ROOTFS/usr/share/fonts/" 2>/dev/null

# --- recursos graficos de LunaSysMgr ---
# Sin images/ la pantalla de bloqueo sale negra: no hay candado, ni chrome, ni
# iconos. Son 286 ficheros. Salio de comparar el rootfs contra el de la VM de
# Ubuntu 12.04, que es la referencia que si funciona.
mkdir -p "$ROOTFS"/usr/palm/sysmgr/{images,localization,low-memory,uiComponents}
cp -rf "$LS"/images/*        "$ROOTFS/usr/palm/sysmgr/images/" 2>/dev/null
cp -rf "$LS"/low-memory/*    "$ROOTFS/usr/palm/sysmgr/low-memory/" 2>/dev/null
cp -rf "$LS"/uiComponents/*  "$ROOTFS/usr/palm/sysmgr/uiComponents/" 2>/dev/null

# --- esquemas y politicas que el script de HP tambien colocaba ---
mkdir -p "$ROOTFS"/etc/palm/schemas "$ROOTFS"/etc/palm/db_kinds "$ROOTFS"/etc/palm/db/permissions
cp -rf "$LS"/conf/*.schema "$ROOTFS/etc/palm/schemas/" 2>/dev/null
cp -f "$LS"/mojodb/com.palm.securitypolicy        "$ROOTFS/etc/palm/db_kinds/" 2>/dev/null
cp -f "$LS"/mojodb/com.palm.securitypolicy.device "$ROOTFS/etc/palm/db_kinds/" 2>/dev/null
cp -f "$LS"/mojodb/com.palm.securitypolicy.permissions "$ROOTFS/etc/palm/db/permissions/com.palm.securitypolicy" 2>/dev/null
mkdir -p "$ROOTFS"/etc/palm/launcher3
cp -rf "$LS"/conf/launcher3/* "$ROOTFS/etc/palm/launcher3/" 2>/dev/null


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

# --- servicios: ficheros de bus y binarios ---
# Cada componente trae en desktop-support/ los cuatro ficheros que ls-hubd
# necesita, siempre con el mismo patron. En vez de enumerarlos uno a uno (que es
# lo que hacia el script de HP, linea a linea), se recorren todos.
#
#   *.json.prv    -> roles/prv/<nombre>.json      quien puede llamar a quien
#   *.json.pub    -> roles/pub/<nombre>.json
#   *.service.prv -> system-services/<n>.service  como arrancarlo (bus privado)
#   *.service.pub -> services/<n>.service
#   *.service     -> a los dos (db8 solo trae uno)
# Algunos componentes los guardan en desktop-support/ y otros en service/.
for DS in "$C"/*/desktop-support "$C"/*/service; do
    [ -d "$DS" ] || continue
    for f in "$DS"/com.palm.*.json.prv;    do [ -e "$f" ] && cp -f "$f" "$ROOTFS/usr/share/ls2/roles/prv/$(basename "$f" .json.prv).json"; done
    for f in "$DS"/com.palm.*.json.pub;    do [ -e "$f" ] && cp -f "$f" "$ROOTFS/usr/share/ls2/roles/pub/$(basename "$f" .json.pub).json"; done
    for f in "$DS"/com.palm.*.service.prv; do [ -e "$f" ] && cp -f "$f" "$ROOTFS/usr/share/ls2/system-services/$(basename "$f" .service.prv).service"; done
    for f in "$DS"/com.palm.*.service.pub; do [ -e "$f" ] && cp -f "$f" "$ROOTFS/usr/share/ls2/services/$(basename "$f" .service.pub).service"; done
    for f in "$DS"/com.palm.*.service;     do [ -e "$f" ] && cp -f "$f" "$ROOTFS/usr/share/ls2/services/" && cp -f "$f" "$ROOTFS/usr/share/ls2/system-services/"; done
done

# Los binarios que esos .service declaran. HP los reunia todos en /usr/lib/luna
# aunque los Exec= apunten a tres sitios distintos (/usr/lib/luna, /usr/local/luna
# y /usr/bin), asi que se hace igual: se toma el nombre del binario, se busca en
# staging y se copia alli.
for sf in "$ROOTFS"/usr/share/ls2/services/*.service "$ROOTFS"/usr/share/ls2/system-services/*.service; do
    [ -e "$sf" ] || continue
    exe=$(sed -n 's|^Exec=[^ ]*/\([^ /]*\).*|\1|p' "$sf" | head -1)
    [ -n "$exe" ] || continue
    if [ ! -e "$ROOTFS/usr/lib/luna/$exe" ]; then
        for d in "$S/usr/sbin" "$S/usr/bin" "$S/sbin" "$S/bin"; do
            [ -x "$d/$exe" ] && cp -f "$d/$exe" "$ROOTFS/usr/lib/luna/" && break
        done
    fi
done

# Los .service traen "Exec=/usr/lib/luna/..." absoluto, que en el dispositivo era
# correcto. Aqui ls-hubd corre FUERA del bwrap (solo LunaSysMgr entra), asi que
# resolveria esa ruta contra el sistema de verdad, donde no hay nada. Se
# reapunta al rootfs. HP resolvia lo mismo con symlinks desde /usr/lib/luna,
# pero eso exige root y toca el sistema.
sed -i -E "s|^Exec=[^ ]*/([^ /]+)|Exec=$ROOTFS/usr/lib/luna/\\1|" \
    "$ROOTFS"/usr/share/ls2/services/*.service \
    "$ROOTFS"/usr/share/ls2/system-services/*.service 2>/dev/null

# Los dos stubs de servicio JS. HP los coloca uno a uno en su script; son los
# que responden a com.palm.location y com.palm.connectionmanager, y sin ellos
# el calendario falla con "getCalendars call failed" y el log se llena de
# "com.palm.connectionmanager is not running".
for par in "mojolocation-stub:com.palm.location" \
           "pmnetconfigmanager-stub:com.palm.connectionmanager"; do
    comp=${par%%:*}; svc=${par##*:}
    [ -d "$C/$comp" ] || continue
    mkdir -p "$ROOTFS/usr/palm/services/$svc"
    cp -rf "$C/$comp"/*.json "$C/$comp"/*.js "$ROOTFS/usr/palm/services/$svc/" 2>/dev/null
    cp -rf "$C/$comp"/files/sysbus/*.json "$ROOTFS/usr/share/ls2/roles/prv/" 2>/dev/null
    cp -rf "$C/$comp"/files/sysbus/*.json "$ROOTFS/usr/share/ls2/roles/pub/" 2>/dev/null
done

# El lanzador de servicios JS, que es quien los arranca de verdad.
if [ -d "$S/usr/palm/services/jsservicelauncher" ]; then
    mkdir -p "$ROOTFS/usr/palm/services/jsservicelauncher"
    cp -f "$S"/usr/palm/services/jsservicelauncher/* "$ROOTFS/usr/palm/services/jsservicelauncher/" 2>/dev/null
fi

# El puente de servicios de las apps (PalmServiceBridge) se registra en el bus
# como "com.palm.webappmgr.bridge". Ningun rol del drop de escritorio declara
# ese nombre --- es nuestro --- asi que se anade aqui, sobre el fichero ya
# copiado, en vez de tocar el original de HP. Sin esto ls-hubd niega todo lo
# saliente del puente y las apps no pueden consultar com.palm.db.
for rf in "$ROOTFS"/usr/share/ls2/roles/prv/com.palm.webappmgr.json \
          "$ROOTFS"/usr/share/ls2/roles/pub/com.palm.webappmgr.json; do
    [ -e "$rf" ] || continue
    python3 - "$rf" <<'PYEOF'
import json, sys
ruta = sys.argv[1]
with open(ruta) as fh:
    datos = json.load(fh)
perms = datos.setdefault("permissions", [])
nombres = datos.setdefault("role", {}).setdefault("allowedNames", [])
if "com.palm.webappmgr.bridge" not in nombres:
    nombres.append("com.palm.webappmgr.bridge")
if not any(p.get("service") == "com.palm.webappmgr.bridge" for p in perms):
    perms.append({"service": "com.palm.webappmgr.bridge",
                  "inbound": ["*"], "outbound": ["*"]})
    with open(ruta, "w") as fh:
        json.dump(datos, fh, indent=4)
PYEOF
done

# Un .service cuyo binario no tenemos solo sirve para que ls-hubd falle al
# intentar arrancarlo bajo demanda (hoy: BrowserServer, que es el camino NPAPI
# del navegador y no se construye). Se retiran.
for sf in "$ROOTFS"/usr/share/ls2/services/*.service "$ROOTFS"/usr/share/ls2/system-services/*.service; do
    [ -e "$sf" ] || continue
    exe=$(sed -n 's|^Exec=[^ ]*/\([^ /]*\).*|\1|p' "$sf" | head -1)
    [ -n "$exe" ] && [ ! -e "$ROOTFS/usr/lib/luna/$exe" ] && rm -f "$sf"
done

# mojodb-luna arranca con "-c /etc/palm/mojodb.conf /var/db" y ese .conf vive
# dentro del fuente de db8, no en desktop-support.
cp -f "$C"/db8/src/db-luna/mojodb.conf "$ROOTFS/etc/palm/" 2>/dev/null
mkdir -p "$ROOTFS"/var/db

# Directorios de trabajo que cada servicio espera encontrar ya creados.
# Salieron de arrancarlos y leer por que morian.
# Los kinds y permisos de db8 que no vienen de core-apps ni de app-services.
# Sin ellos com.palm.db no tiene esquema y toda consulta de las apps falla
# --- que es por lo que email y calendario salian vacios.
mkdir -p "$ROOTFS"/etc/palm/db/{kinds,permissions} "$ROOTFS"/etc/palm/tempdb/kinds
for d in "$C"/activitymanager/files/db8 "$C"/mojomail/*/files/db8 "$C"/isis-browser/db; do
    [ -d "$d/kinds" ]       && cp -rf "$d"/kinds/*       "$ROOTFS/etc/palm/db/kinds/"       2>/dev/null
    [ -d "$d/permissions" ] && cp -rf "$d"/permissions/* "$ROOTFS/etc/palm/db/permissions/" 2>/dev/null
done
# Las cuentas traen los suyos aparte, y ademas una base temporal.
A="$C/app-services/com.palm.service.accounts"
cp -f  "$A"/desktop/com.palm.account.credentials "$ROOTFS/etc/palm/db/kinds/" 2>/dev/null
cp -rf "$A"/tempdb/kinds/*                       "$ROOTFS/etc/palm/tempdb/kinds/" 2>/dev/null

mkdir -p "$ROOTFS"/var/palm/data/universalsearchmgr/searchplugins
mkdir -p "$ROOTFS"/var/palm/data "$ROOTFS"/var/file-cache
mkdir -p "$S"/var/file-cache          # filecache lo pide bajo el prefijo del build

# fonts.conf propio: anade las fuentes de webOS a las del sistema en vez de
# sustituirlas. La ruta va absoluta porque fontconfig no expande variables.
cat > "$ROOTFS/etc/fonts.conf" <<FC
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <include ignore_missing="yes">/etc/fonts/fonts.conf</include>
  <dir>$ROOTFS/usr/share/fonts</dir>
</fontconfig>
FC

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
echo "  apps:                $(ls "$ROOTFS/usr/palm/applications" 2>/dev/null | wc -l)"
echo "  servicios:           $(ls "$ROOTFS/usr/palm/services" 2>/dev/null | wc -l)"
echo "  frameworks:          $(ls "$ROOTFS/usr/palm/frameworks" 2>/dev/null | wc -l)"
echo "  kinds de db8:        $(ls "$ROOTFS/etc/palm/db/kinds" 2>/dev/null | wc -l)"

echo "  etc/palm:            $(ls "$ROOTFS/etc/palm" 2>/dev/null | wc -l) entradas"
echo "  etc/ls2:             $(ls "$ROOTFS/etc/ls2" 2>/dev/null | wc -l) entradas"
echo "  luna-systemui:       $(ls "$ROOTFS/usr/lib/luna/system/luna-systemui" 2>/dev/null | wc -l) entradas"
echo "  luna-applauncher:    $(ls "$ROOTFS/usr/lib/luna/system/luna-applauncher" 2>/dev/null | wc -l) entradas"
echo "  roles del bus:       $(ls "$ROOTFS/usr/share/ls2/roles/prv" "$ROOTFS/usr/share/ls2/roles/pub" 2>/dev/null | grep -c json)"
