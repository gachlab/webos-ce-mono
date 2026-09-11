#!/bin/bash
# Assembles the runtime tree LunaSysMgr expects, replicating what
# build-webos-desktop.sh did to $ROOTFS. Without it the binary starts but draws
# an empty window: it finds neither configuration nor UI resources.
set -u
R="$(cd "$(dirname "$0")/.." && pwd)"
C="$R/components"
ROOTFS="${1:-$R/build/rootfs}"
S="$R/build/staging"

mkdir -p "$ROOTFS"/{etc/palm/pubsub_handlers,etc/ls2,usr/lib/luna/system,usr/palm/sounds}
mkdir -p "$ROOTFS"/usr/share/ls2/{roles/prv,roles/pub,services,system-services}
mkdir -p "$ROOTFS"/usr/lib/luna/customization "$ROOTFS"/var/{db,luna,palm} "$ROOTFS"/usr/share/fonts

# --- the bus ---
# Its .conf files are written at the very end of this script; see there for why.

# --- LunaSysMgr: configuration, bus roles, sounds ---
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
cp -f "$S/bin/LunaSysMgr" "$ROOTFS/usr/lib/luna/LunaSysMgr"

# --- WebAppMgr: the process that runs the web apps ---
# Not launched by hand. It is an LS2 service: LunaSysMgr talks to it over the
# bus (WebAppMgrProxy) and ls-hubd starts it from the Exec= in these .service
# files.
WAM="$R/components/webappmanager"
if [ -x "$S/bin/WebAppMgr" ]; then
    cp -f "$S/bin/WebAppMgr" "$ROOTFS/usr/lib/luna/WebAppMgr"
    cp -f "$WAM"/desktop-support/com.palm.webappmgr.json.prv    "$ROOTFS/usr/share/ls2/roles/prv/com.palm.webappmgr.json"
    cp -f "$WAM"/desktop-support/com.palm.webappmgr.json.pub    "$ROOTFS/usr/share/ls2/roles/pub/com.palm.webappmgr.json"
    cp -f "$WAM"/desktop-support/com.palm.webappmgr.service.prv "$ROOTFS/usr/share/ls2/system-services/com.palm.webappmgr.service"
    cp -f "$WAM"/desktop-support/com.palm.webappmgr.service.pub "$ROOTFS/usr/share/ls2/services/com.palm.webappmgr.service"
fi


# --- the UI: these two are what actually draws ---
mkdir -p "$ROOTFS/usr/lib/luna/system/luna-systemui"
cp -rf "$C"/luna-systemui/* "$ROOTFS/usr/lib/luna/system/luna-systemui/" 2>/dev/null
# The wallpaper lives inside a tar, not loose (same as the Prelude fonts in
# fonts.tgz). Without extracting it the lock screen comes up black: inside is
# bluerocks.png, webOS's default background.
tar xf "$C"/luna-systemui/images/wallpaper.tar \
    -C "$ROOTFS/usr/lib/luna/system/luna-systemui/images" 2>/dev/null
mkdir -p "$ROOTFS/usr/lib/luna/system/luna-applauncher"
cp -rf "$C"/luna-applauncher/* "$ROOTFS/usr/lib/luna/system/luna-applauncher/" 2>/dev/null
cp -f "$LS"/desktop-support/appinfo.json "$ROOTFS/usr/lib/luna/system/luna-applauncher/appinfo.json"

# --- base configuration and fonts ---
cp -f "$C"/luna-init/files/conf/*.json "$ROOTFS/usr/palm/" 2>/dev/null
cp -f "$C"/luna-init/files/conf/fonts/*.xml "$ROOTFS/usr/share/fonts/" 2>/dev/null
cp -rf "$C"/isis-fonts/* "$ROOTFS/usr/share/fonts/" 2>/dev/null
# The Prelude fonts live inside a tarball, not loose. They are webOS's
# typeface: without them the lock screen's big clock does not draw.
tar xzf "$C"/luna-init/files/conf/fonts/fonts.tgz -C "$ROOTFS/usr/share/fonts/" 2>/dev/null

# --- LunaSysMgr's graphical resources ---
# Without images/ the lock screen is black: no padlock, no chrome, no icons.
# 286 files. Found by diffing this rootfs against the Ubuntu 12.04 VM's, which
# is the reference that does work.
mkdir -p "$ROOTFS"/usr/palm/sysmgr/{images,localization,low-memory,uiComponents}
cp -rf "$LS"/images/*        "$ROOTFS/usr/palm/sysmgr/images/" 2>/dev/null
cp -rf "$LS"/low-memory/*    "$ROOTFS/usr/palm/sysmgr/low-memory/" 2>/dev/null
cp -rf "$LS"/uiComponents/*  "$ROOTFS/usr/palm/sysmgr/uiComponents/" 2>/dev/null

# --- schemas and policies HP's script also placed ---
mkdir -p "$ROOTFS"/etc/palm/schemas "$ROOTFS"/etc/palm/db_kinds "$ROOTFS"/etc/palm/db/permissions
cp -rf "$LS"/conf/*.schema "$ROOTFS/etc/palm/schemas/" 2>/dev/null
cp -f "$LS"/mojodb/com.palm.securitypolicy        "$ROOTFS/etc/palm/db_kinds/" 2>/dev/null
cp -f "$LS"/mojodb/com.palm.securitypolicy.device "$ROOTFS/etc/palm/db_kinds/" 2>/dev/null
cp -f "$LS"/mojodb/com.palm.securitypolicy.permissions "$ROOTFS/etc/palm/db/permissions/com.palm.securitypolicy" 2>/dev/null
mkdir -p "$ROOTFS"/etc/palm/launcher3
cp -rf "$LS"/conf/launcher3/* "$ROOTFS/etc/palm/launcher3/" 2>/dev/null


# --- Content: apps, frameworks and services (copy-only components) ---
mkdir -p "$ROOTFS"/usr/palm/{applications,services,frameworks} "$ROOTFS"/etc/palm/db/{kinds,permissions}

# Enyo: the framework every app is written on top of
mkdir -p "$ROOTFS/usr/palm/frameworks/enyo/0.10/framework"
cp -rf "$C"/enyo-1.0/framework/* "$ROOTFS/usr/palm/frameworks/enyo/0.10/framework/" 2>/dev/null
ln -sfn 0.10 "$ROOTFS/usr/palm/frameworks/enyo/version" 2>/dev/null

# HP's apps. Each ships its own db8 kinds and permissions.
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

# Tests are excluded. They are not just dead weight: the frameworks ship mock
# versions of their own modules under jasminetest/, and the service test helpers
# call MojoLoader methods that do not exist outside a test harness.
copy_without_tests() {   # copy_without_tests <source dir> <destination dir>
    mkdir -p "$2"
    (cd "$1" && find . \( -type d -name test -o -type d -name tests \
                        -o -type d -name spec -o -type d -name jasminetest \) -prune -o \
                     -type f -print) \
        | while read -r f; do
            mkdir -p "$2/$(dirname "$f")"
            cp -f "$1/$f" "$2/$f"
        done
}

# Frameworks: each one under <name>/version/1.0/
for GRUPO in foundation-frameworks mojoservice-frameworks loadable-frameworks; do
    for FW in "$C"/$GRUPO/*/; do
        n=$(basename "$FW"); [ "$n" = "." ] && continue
        case "$n" in .git|*.md|files) continue;; esac
        copy_without_tests "${FW%/}" "$ROOTFS/usr/palm/frameworks/$n/version/1.0"
    done
done
mkdir -p "$ROOTFS/usr/palm/frameworks/underscore/version/1.0"
cp -rf "$C"/underscore/* "$ROOTFS/usr/palm/frameworks/underscore/version/1.0/" 2>/dev/null
cp -f "$C"/mojoloader/mojoloader.js "$ROOTFS/usr/palm/frameworks/" 2>/dev/null

# --- services: bus files and binaries ---
# Each component ships in desktop-support/ the four files ls-hubd needs, always
# with the same pattern. Rather than listing them one by one (which is what HP's
# script did, line by line), we walk them all.
#
#   *.json.prv    -> roles/prv/<name>.json       who may call whom
#   *.json.pub    -> roles/pub/<name>.json
#   *.service.prv -> system-services/<n>.service  how to start it (private bus)
#   *.service.pub -> services/<n>.service
#   *.service     -> both (db8 only ships one)
# Some components keep them in desktop-support/ and others in service/.
for DS in "$C"/*/desktop-support "$C"/*/service; do
    [ -d "$DS" ] || continue
    for f in "$DS"/com.palm.*.json.prv;    do [ -e "$f" ] && cp -f "$f" "$ROOTFS/usr/share/ls2/roles/prv/$(basename "$f" .json.prv).json"; done
    for f in "$DS"/com.palm.*.json.pub;    do [ -e "$f" ] && cp -f "$f" "$ROOTFS/usr/share/ls2/roles/pub/$(basename "$f" .json.pub).json"; done
    for f in "$DS"/com.palm.*.service.prv; do [ -e "$f" ] && cp -f "$f" "$ROOTFS/usr/share/ls2/system-services/$(basename "$f" .service.prv).service"; done
    for f in "$DS"/com.palm.*.service.pub; do [ -e "$f" ] && cp -f "$f" "$ROOTFS/usr/share/ls2/services/$(basename "$f" .service.pub).service"; done
    for f in "$DS"/com.palm.*.service;     do [ -e "$f" ] && cp -f "$f" "$ROOTFS/usr/share/ls2/services/" && cp -f "$f" "$ROOTFS/usr/share/ls2/system-services/"; done
done

# The binaries those .service files declare. HP gathered them all under
# /usr/lib/luna even though the Exec= lines point at three different places
# (/usr/lib/luna, /usr/local/luna and /usr/bin), so we do the same: take the
# binary name, find it in staging and copy it there.
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

# The .service files carry an absolute "Exec=/usr/lib/luna/...", which was
# correct on the device. Here ls-hubd runs OUTSIDE the bwrap (only LunaSysMgr
# goes in), so it would resolve that against the real system, where there is
# nothing. It is repointed at the rootfs. HP solved the same thing with symlinks
# from /usr/lib/luna, but that needs root and touches the system.
sed -i -E "s|^Exec=[^ ]*/([^ /]+)|Exec=$ROOTFS/usr/lib/luna/\\1|" \
    "$ROOTFS"/usr/share/ls2/services/*.service \
    "$ROOTFS"/usr/share/ls2/system-services/*.service 2>/dev/null

# The two JS service stubs. HP places them one by one in its script; they are
# what answers com.palm.location and com.palm.connectionmanager, and without
# them the calendar fails with "getCalendars call failed" and the log fills up
# with "com.palm.connectionmanager is not running".
for par in "mojolocation-stub:com.palm.location" \
           "pmnetconfigmanager-stub:com.palm.connectionmanager"; do
    comp=${par%%:*}; svc=${par##*:}
    [ -d "$C/$comp" ] || continue
    mkdir -p "$ROOTFS/usr/palm/services/$svc"
    cp -rf "$C/$comp"/*.json "$C/$comp"/*.js "$ROOTFS/usr/palm/services/$svc/" 2>/dev/null
    cp -rf "$C/$comp"/files/sysbus/*.json "$ROOTFS/usr/share/ls2/roles/prv/" 2>/dev/null
    cp -rf "$C/$comp"/files/sysbus/*.json "$ROOTFS/usr/share/ls2/roles/pub/" 2>/dev/null
done

# The JS service launcher, which is what actually starts them.
if [ -d "$S/usr/palm/services/jsservicelauncher" ]; then
    mkdir -p "$ROOTFS/usr/palm/services/jsservicelauncher"
    cp -f "$S"/usr/palm/services/jsservicelauncher/* "$ROOTFS/usr/palm/services/jsservicelauncher/" 2>/dev/null
fi

# The apps' service bridge (PalmServiceBridge) registers on the bus as
# "com.palm.webappmgr.bridge". No role in the desktop drop declares that name
# -- it is ours -- so it is added here, on the already-copied file, rather than
# touching HP's original. Without it ls-hubd denies everything outbound from the
# bridge and the apps cannot query com.palm.db.
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

# A .service whose binary we do not have only makes ls-hubd fail when it tries
# to start it on demand (today: BrowserServer, the browser's NPAPI path, which
# we do not build). Those are removed.
for sf in "$ROOTFS"/usr/share/ls2/services/*.service "$ROOTFS"/usr/share/ls2/system-services/*.service; do
    [ -e "$sf" ] || continue
    exe=$(sed -n 's|^Exec=[^ ]*/\([^ /]*\).*|\1|p' "$sf" | head -1)
    [ -n "$exe" ] && [ ! -e "$ROOTFS/usr/lib/luna/$exe" ] && rm -f "$sf"
done

# mojodb-luna starts with "-c /etc/palm/mojodb.conf /var/db", and that .conf
# lives inside db8's source, not in desktop-support.
cp -f "$C"/db8/src/db-luna/mojodb.conf "$ROOTFS/etc/palm/" 2>/dev/null
mkdir -p "$ROOTFS"/var/db

# Working directories each service expects to already exist. Found by starting
# them and reading why they died.
# The db8 kinds and permissions that do not come from core-apps or app-services.
# Without them com.palm.db has no schema and every app query fails -- which is
# why email and calendar came up empty.
mkdir -p "$ROOTFS"/etc/palm/db/{kinds,permissions} "$ROOTFS"/etc/palm/tempdb/kinds
for d in "$C"/activitymanager/files/db8 "$C"/mojomail/*/files/db8 "$C"/isis-browser/db; do
    [ -d "$d/kinds" ]       && cp -rf "$d"/kinds/*       "$ROOTFS/etc/palm/db/kinds/"       2>/dev/null
    [ -d "$d/permissions" ] && cp -rf "$d"/permissions/* "$ROOTFS/etc/palm/db/permissions/" 2>/dev/null
done
# Accounts ship theirs separately, plus a temporary database.
A="$C/app-services/com.palm.service.accounts"
cp -f  "$A"/desktop/com.palm.account.credentials "$ROOTFS/etc/palm/db/kinds/" 2>/dev/null
cp -rf "$A"/tempdb/kinds/*                       "$ROOTFS/etc/palm/tempdb/kinds/" 2>/dev/null

mkdir -p "$ROOTFS"/var/palm/data/universalsearchmgr/searchplugins
mkdir -p "$ROOTFS"/var/palm/data "$ROOTFS"/var/file-cache
mkdir -p "$S"/var/file-cache          # filecache looks for it under the build prefix

# Our own fonts.conf: adds webOS's fonts to the system's rather than replacing
# them. The path is absolute because fontconfig does not expand variables.
cat > "$ROOTFS/etc/fonts.conf" <<FC
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <include ignore_missing="yes">/etc/fonts/fonts.conf</include>
  <dir>$ROOTFS/usr/share/fonts</dir>
</fontconfig>
FC

# --- repoint luna.conf's paths at the rootfs ---
# They are the device's absolute paths (/usr/palm/..., /var/luna/...). bwrap can
# only mount /etc/palm, the one path hardcoded in the code (Settings.cpp); the
# others cannot be mounted over because /usr and /var are read-only inside the
# namespace and bwrap cannot create the mountpoint there. It is not needed
# either: luna.conf exists precisely to configure them. Without this LunaSysMgr
# draws the shell but finds NO apps at all.
mkdir -p "$ROOTFS"/usr/lib/luna/applications "$ROOTFS"/usr/palm/sysmgr/{images,localization} \
         "$ROOTFS"/var/luna/{launchpoints,preferences}
sed -i -E "/^(ApplicationPath|SystemPath|SystemResourcesPath|SystemLocalePath|AppLauncherPath|LaunchPointsPath|PreferencesPath)=/ s#(=|:)/#\\1$ROOTFS/#g" \
    "$ROOTFS/etc/palm/luna.conf"

echo "rootfs assembled at $ROOTFS"
echo "  apps:                $(ls "$ROOTFS/usr/palm/applications" 2>/dev/null | wc -l)"
echo "  services:            $(ls "$ROOTFS/usr/palm/services" 2>/dev/null | wc -l)"
echo "  frameworks:          $(ls "$ROOTFS/usr/palm/frameworks" 2>/dev/null | wc -l)"
echo "  db8 kinds:           $(ls "$ROOTFS/etc/palm/db/kinds" 2>/dev/null | wc -l)"

echo "  etc/palm:            $(ls "$ROOTFS/etc/palm" 2>/dev/null | wc -l) entries"
echo "  luna-systemui:       $(ls "$ROOTFS/usr/lib/luna/system/luna-systemui" 2>/dev/null | wc -l) entries"
echo "  luna-applauncher:    $(ls "$ROOTFS/usr/lib/luna/system/luna-applauncher" 2>/dev/null | wc -l) entries"
echo "  bus roles:           $(ls "$ROOTFS/usr/share/ls2/roles/prv" "$ROOTFS/usr/share/ls2/roles/pub" 2>/dev/null | grep -c json)"

# --- JavaScript services: launcher, frameworks, the services themselves ---
#
# run-js-service runs
#   $NODE /usr/palm/services/jsservicelauncher/bootstrap-node.js <service path>
# with NODE_PATH=/usr/palm/frameworks:/usr/palm/nodejs, and refuses any service
# path that is not under /usr/palm/services. So the tree goes where it looks.
mkdir -p "$ROOTFS/usr/palm/services/jsservicelauncher" "$ROOTFS/usr/palm/frameworks"

MSL="$R/components/mojoservicelauncher"
if [ -d "$MSL" ]; then
    cp -f "$MSL"/bootstrap-node.js "$MSL"/fork_server.js "$MSL"/palm_bus_config.json \
          "$ROOTFS/usr/palm/services/jsservicelauncher/" 2>/dev/null || true
    [ -d "$MSL/jslauncher" ] && cp -rf "$MSL/jslauncher" "$ROOTFS/usr/palm/services/jsservicelauncher/"
fi
cp -f "$R/components/mojoloader/mojoloader.js" "$ROOTFS/usr/palm/frameworks/" 2>/dev/null || true

# The services. Only the ones that are pure JavaScript are useful yet.
for svc in "$R"/components/mojolocation-stub "$R"/components/pmnetconfigmanager-stub \
           "$R"/components/app-services/com.palm.service.*; do
    [ -f "$svc/services.json" ] || continue
    id="$(python3 -c "import json,sys; print(json.load(open(sys.argv[1])).get('id', ''))" "$svc/services.json" 2>/dev/null || true)"
    # HP's stubs name themselves with "id"; the app-services do not have one, and
    # their directory already is the service name. Without this fallback all
    # four were skipped outright -- no files, no role, no .service.
    id="${id:-$(basename "$svc")}"
    [ -n "$id" ] || continue
    copy_without_tests "$svc" "$ROOTFS/usr/palm/services/$id"

    # Its bus files: the role in files/sysbus, the .service in desktop-support.
    # Without the role the hub refuses the service's own name; without the
    # .service it has no way to start it when something calls.
    cp -f "$svc"/files/sysbus/*.json "$ROOTFS/usr/share/ls2/roles/pub/" 2>/dev/null || true
    cp -f "$svc"/files/sysbus/*.json "$ROOTFS/usr/share/ls2/roles/prv/" 2>/dev/null || true
    cp -f "$svc"/desktop-support/*.service "$ROOTFS/usr/share/ls2/services/" 2>/dev/null || true
    cp -f "$svc"/desktop-support/*.service "$ROOTFS/usr/share/ls2/system-services/" 2>/dev/null || true
done

# Directories configurator walks and HP's rootfs always had, even when empty.
# Missing, each costs an "[error] Failed to open directory" in configurator's log
# on every init -- no configuration fails because of them, but the errors bury
# the ones that matter.
mkdir -p "$ROOTFS/etc/palm/activities" "$ROOTFS/etc/palm/tempdb/permissions" \
         "$ROOTFS/etc/palm/filecache_types"

# db8's data directory. run-lunasysmgr.sh binds it at /var/db so the database
# survives restarting the services; it has to exist for that bind to work.
mkdir -p "$ROOTFS/var/db"

# luna-send's own bus role, from HP's templates in luna-service2.
#
# Without it luna-send reaches the hub as an anonymous client with no
# permissions: the public hub lets that through with a warning, the private one
# refuses it outright, so nothing could call a service on the private bus from a
# terminal. The templates leave the executable as @WEBOS_INSTALL_BINDIR@; the
# hub identifies a caller through /proc/<pid>/exe, so it has to be the path
# luna-send is really run from, which is staging.
for side in pub prv; do
    tpl="$C/luna-service2/files/sysbus/com.palm.lunasend.json.$side.in"
    [ -f "$tpl" ] || continue
    sed "s|@WEBOS_INSTALL_BINDIR@|$S/usr/bin|" "$tpl" \
        > "$ROOTFS/usr/share/ls2/roles/$side/com.palm.lunasend.json"
done

# Account templates. The accounts service lists every <type>/*.json under
# /usr/palm/public/accounts (TEMPLATE_ROOTS in accounts.js). HP ships them with
# the components that own each account type -- mojomail's IMAP, POP and "other
# mail", and palmprofile -- and they were never installed, so the service said
# "Found 0 account templates" and there was no kind of account to add. That is
# the rest of why calendar and email came up empty.
mkdir -p "$ROOTFS/usr/palm/public/accounts"
for d in "$C"/mojomail/*/files/usr/palm/public/accounts/* "$C"/app-services/account-templates/*/*; do
    [ -d "$d" ] || continue
    cp -rf "$d" "$ROOTFS/usr/palm/public/accounts/"
done

# How the hub starts a JavaScript service when something calls it.
#
# ls-hubd runs outside the bwrap namespace on purpose -- it identifies every
# caller through /proc/<pid>/exe -- so an Exec that runs run-js-service directly
# finds no /usr/palm/services and exits with "Invalid service path". The Exec is
# pointed at run-lunasysmgr.sh instead, which enters the same namespace the
# shell uses and runs run-js-service from inside it. The services then start on
# demand and quit when idle, which is how they behave on a device.
sed -i -E "s|^Exec=[^ ]*run-js-service(.*)$|Exec=$R/tools/run-lunasysmgr.sh js-service\1|" \
    "$ROOTFS"/usr/share/ls2/services/*.service \
    "$ROOTFS"/usr/share/ls2/system-services/*.service 2>/dev/null

# And every C++ service the hub might start on demand, for the same reason:
# launched straight from the rootfs path it runs outside the namespace and sees
# none of /etc/palm, /usr/palm or /var/db. See the ns-exec case in
# run-lunasysmgr.sh for what that cost.
sed -i -E "s|^Exec=$ROOTFS/usr/lib/luna/|Exec=$R/tools/run-lunasysmgr.sh ns-exec /usr/lib/luna/|" \
    "$ROOTFS"/usr/share/ls2/services/*.service \
    "$ROOTFS"/usr/share/ls2/system-services/*.service 2>/dev/null

# run-js-service preloads /usr/lib/libmemcpy.so, an optimised memcpy that only
# existed on the device. An empty library stands in for it, so every service
# launch stops printing an ld.so error; the system memcpy is used either way.
if [ ! -e "$ROOTFS/usr/lib/libmemcpy.so" ]; then
    cc -shared -o "$ROOTFS/usr/lib/libmemcpy.so" -x c /dev/null 2>/dev/null || true
fi
echo "  js services:         $(ls "$ROOTFS/usr/palm/services" 2>/dev/null | wc -l) dirs, frameworks: $(ls "$ROOTFS/usr/palm/frameworks" 2>/dev/null | wc -l)"

# --- node: the path HP's own configuration expects ---
#
# Written at the end on purpose. HP's own role files are copied in above and
# would overwrite this one -- which they silently did, so the hub kept reading
# HP's version with no permissions block and the calls stayed denied.
#
# run-js-service runs $NODE=/usr/palm/nodejs/node and loads addons from the same
# directory, and HP's bus role file grants permissions by that exact exeName. So
# rather than teach either of them about a different node, the rootfs provides
# the path they already look for.
#
# HP's own com.palm.nodejs.json has a "role" block and no "permissions" block,
# which luna-service2 requires -- hence the "Unable to get permission from JSON"
# line in the hub log. A permissions block is written alongside it here instead
# of editing HP's file.
NODE_BIN="$(command -v node 2>/dev/null || true)"
if [ -n "$NODE_BIN" ]; then
    mkdir -p "$ROOTFS/usr/palm/nodejs"
    # A mount point, not a symlink. ls-hubd checks who is calling by reading
    # /proc/<pid>/exe, which resolves a symlink to its target -- so the role
    # above would be looked up under the real node path and not found. The run
    # script bind-mounts the real binary onto this file inside the namespace,
    # and then /proc/<pid>/exe reports the path the role names.
    # rm first, and never write through what might be there.
    #
    # An earlier version of this created the mount point with ": > $file" while
    # a symlink to the real node was still sitting at that path. The redirection
    # followed the link and truncated the node installation to zero bytes.
    rm -f "$ROOTFS/usr/palm/nodejs/node"
    : > "$ROOTFS/usr/palm/nodejs/node"
    chmod 0755 "$ROOTFS/usr/palm/nodejs/node"

    for side in pub prv; do
        cat > "$ROOTFS/usr/share/ls2/roles/$side/com.palm.nodejs.json" <<'JSON'
{
    "role": {
        "exeName": "/usr/palm/nodejs/node",
        "type": "privileged",
        "allowedNames": ["", "com.palm.nodejs", "com.palm.service.*", "com.palm.app.*", "*"]
    },
    "permissions": [
        {
            "service": "com.palm.nodejs",
            "inbound": ["*"],
            "outbound": ["*"]
        }
    ]
}
JSON
    done
    # The addons and the compat shim come from staging, where the node stage of
    # tools/build.sh installed them.
    cp -f "$S/usr/palm/nodejs/"*.node "$ROOTFS/usr/palm/nodejs/" 2>/dev/null || true
    cp -f "$R/components/node-v8-shim/js/webos-node-compat.js" "$ROOTFS/usr/palm/nodejs/"
    echo "  node:                $NODE_BIN bound at /usr/palm/nodejs/node"
fi

# The bus's own .conf files, written last and only when they change.
#
# ls2's .conf files point at /usr/share/ls2/..., where ls-hubd could find
# neither the roles nor the services, so they are repointed at the rootfs.
#
# ls-hubd watches the directory its .conf lives in with inotify and reloads --
# conf, roles, service files -- whenever the .conf is written. Copying it early
# and fixing its paths late made a running hub reload twice in the middle of
# this script: once reading HP's unrepointed paths ("Error opening directory
# /usr/share/ls2/roles/prv"), and once before the JavaScript services' .service
# files were back in place. Nothing touched the .conf after that, so the hub kept
# a view with those services missing -- "Service not listed in service files" --
# with every file correct on disk.
#
# So each .conf is built beside the real one and moved over it only if it
# differs, and the running hubs get one SIGHUP once everything they read is
# complete. That makes this the last step of the script: the first version of
# this fix sat before the JavaScript services section, so the SIGHUP landed a
# second before their .service files and role were written, and the hub kept
# the same incomplete view.
for conf in "$R"/desktop-support/ls2/*.conf; do
    dest="$ROOTFS/etc/ls2/$(basename "$conf")"
    sed -E "s#^(Directories=)/#\\1$ROOTFS/#" "$conf" > "$dest.new"
    if cmp -s "$dest.new" "$dest"; then
        rm -f "$dest.new"
    else
        mv -f "$dest.new" "$dest"
    fi
done

hubs=""
for d in /proc/[0-9]*; do
    [ "$(readlink "$d/exe" 2>/dev/null)" = "$S/usr/sbin/ls-hubd" ] && hubs="$hubs ${d#/proc/}"
done
if [ -n "$hubs" ]; then
    kill -HUP $hubs 2>/dev/null
    echo "  ls-hubd:             reloaded ($(echo $hubs | wc -w) running)"
fi
echo "  etc/ls2:             $(ls "$ROOTFS/etc/ls2" 2>/dev/null | wc -l) entries"
