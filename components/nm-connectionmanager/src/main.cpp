/* @@@LICENSE
 *
 * Copyright (c) 2026 webOS CE modern build
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * LICENSE@@@ */

//
// com.palm.connectionmanager, answered from NetworkManager.
//
// HP's own answer to this call is components/pmnetconfigmanager-stub, a
// JavaScript service that replies with a constant: connected, over wifi, on
// "Open webOS", 192.168.0.0, always. Every consumer in the tree believes it, so
// pulling the cable or switching wifi off changed nothing and the email app kept
// trying to sync against a network that was not there.
//
// Who is listening, measured rather than assumed -- all four subscribe with
// {"subscribe":true} and act on what arrives:
//
//   StatusBarServicesConnector.cpp     the wifi indicator
//   luna-sysservice's NetworkConnectionListener
//                                      fires connectionStateChanged, which is
//                                      what tells the rest of the system it is
//                                      offline
//   BrowserServer.cpp                  isInternetConnectionAvailable
//   activitymanager's ConnectionManagerProxy
//                                      the wifi/wan/*Confidence requirements
//                                      activities are scheduled against
//
// Why C++ and not JavaScript, which is what the stub is: this needs a D-Bus
// client, and gio already provides one to anything that links glib -- which
// every service here does for its main loop. The JavaScript route would have
// meant bundling a D-Bus library and building the npm machinery to vendor it,
// before writing a line of network logic. Subscriptions were NOT the reason:
// they were measured to work from a mojoservice JS service in this port, unlike
// the LS2 signals that palmbus cannot emit (see components/sysfs-powerd).
//
// The mapping from NetworkManager's state to webOS's payload is in
// network_state.h, free of both buses, so tests/network-state.cpp can check it
// without a D-Bus daemon and without ls-hubd.
//

#include "network_state.h"

#include <luna-service2/lunaservice.h>

#include <gio/gio.h>
#include <glib.h>
#include <glib-unix.h>

#include <string>

namespace {

const char kServiceName[] = "com.palm.connectionmanager";
const char kCategory[] = "/";

// Both spellings, because HP's own callers disagree: activitymanager calls
// getStatus, while the status bar, luna-sysservice and BrowserServer call
// getstatus. The stub declared both in its services.json and so does this.
const char* const kStatusMethods[] = { "getStatus", "getstatus", nullptr };

// com.palm.wifi, the second name this process owns. It is the same network read
// from the same NetworkManager state, in the shape the status bar's wifi
// indicator wants -- see network_state.h. Registering both names in one process
// keeps them from ever disagreeing about what the radio is doing.
const char kWifiServiceName[] = "com.palm.wifi";

const char kNmService[] = "org.freedesktop.NetworkManager";
const char kNmPath[] = "/org/freedesktop/NetworkManager";
const char kNmIface[] = "org.freedesktop.NetworkManager";

GMainLoop* g_loop = nullptr;
LSPalmService* g_service = nullptr;
LSPalmService* g_wifiService = nullptr;
GDBusConnection* g_system = nullptr;
NmNet::NetworkState g_state;
std::string g_lastPayload;
std::string g_lastWifiKey;
guint g_refreshPending = 0;

void logAndFree(const char* where, LSError& error)
{
    g_warning("nm-connectionmanager: %s: %s", where,
              error.message ? error.message : "(no message)");
    LSErrorFree(&error);
}

// --- reading NetworkManager -------------------------------------------------

// One property, or nullptr. A device can disappear between being listed and
// being read -- unplugging a USB adapter, a container going away -- and that is
// ordinary, so a failure here is silent rather than a warning on every change.
GVariant* property(const char* path, const char* iface, const char* name)
{
    if (!g_system)
        return nullptr;
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        g_system, kNmService, path, "org.freedesktop.DBus.Properties", "Get",
        g_variant_new("(ss)", iface, name), G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error);
    if (!reply) {
        g_clear_error(&error);
        return nullptr;
    }
    GVariant* boxed = nullptr;
    g_variant_get(reply, "(v)", &boxed);
    g_variant_unref(reply);
    return boxed;
}

guint32 uintProperty(const char* path, const char* iface, const char* name, guint32 fallback)
{
    GVariant* v = property(path, iface, name);
    if (!v)
        return fallback;
    guint32 out = fallback;
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_UINT32))
        out = g_variant_get_uint32(v);
    else if (g_variant_is_of_type(v, G_VARIANT_TYPE_BYTE))
        out = g_variant_get_byte(v);
    g_variant_unref(v);
    return out;
}

std::string stringProperty(const char* path, const char* iface, const char* name)
{
    GVariant* v = property(path, iface, name);
    if (!v)
        return std::string();
    std::string out;
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_STRING)
        || g_variant_is_of_type(v, G_VARIANT_TYPE_OBJECT_PATH)) {
        const char* s = g_variant_get_string(v, nullptr);
        if (s)
            out = s;
    }
    g_variant_unref(v);
    return out;
}

// An SSID is a byte array, not a string: it is whatever the access point
// advertises. It is carried through as bytes and escaped when the payload is
// built; network_state.h says why that matters.
std::string ssidOf(const std::string& apPath)
{
    if (apPath.empty() || apPath == "/")
        return std::string();
    GVariant* v = property(apPath.c_str(), "org.freedesktop.NetworkManager.AccessPoint", "Ssid");
    if (!v)
        return std::string();
    std::string out;
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_BYTESTRING)) {
        gsize length = 0;
        const guchar* bytes = static_cast<const guchar*>(
            g_variant_get_fixed_array(v, &length, sizeof(guchar)));
        if (bytes && length)
            out.assign(reinterpret_cast<const char*>(bytes), length);
    }
    g_variant_unref(v);
    return out;
}

// The first IPv4 address of a device, from its Ip4Config. Without this the
// status bar has a network with no address, which is what HP's stub invented
// (192.168.0.0, a network number that is nobody's address).
std::string addressOf(const char* devicePath)
{
    const std::string configPath =
        stringProperty(devicePath, "org.freedesktop.NetworkManager.Device", "Ip4Config");
    if (configPath.empty() || configPath == "/")
        return std::string();

    GVariant* data = property(configPath.c_str(),
                              "org.freedesktop.NetworkManager.IP4Config", "AddressData");
    if (!data)
        return std::string();

    std::string out;
    GVariantIter iter;
    GVariant* entry = nullptr;
    g_variant_iter_init(&iter, data);
    while (out.empty() && (entry = g_variant_iter_next_value(&iter))) {
        GVariant* address = g_variant_lookup_value(entry, "address", G_VARIANT_TYPE_STRING);
        if (address) {
            const char* s = g_variant_get_string(address, nullptr);
            if (s)
                out = s;
            g_variant_unref(address);
        }
        g_variant_unref(entry);
    }
    g_variant_unref(data);
    return out;
}

// Whether any active connection is a VPN. NM's PrimaryConnection is the tunnel
// itself while one is up -- measured, with the primary connection reading
// type "vpn" over a wifi that was carrying it -- which is why the transport is
// found by walking the devices instead of trusting the primary connection.
bool anyVpnActive()
{
    GVariant* active = property(kNmPath, kNmIface, "ActiveConnections");
    if (!active)
        return false;

    bool found = false;
    GVariantIter iter;
    const char* path = nullptr;
    g_variant_iter_init(&iter, active);
    while (!found && g_variant_iter_next(&iter, "&o", &path)) {
        GVariant* isVpn = property(path, "org.freedesktop.NetworkManager.Connection.Active", "Vpn");
        if (isVpn) {
            found = g_variant_is_of_type(isVpn, G_VARIANT_TYPE_BOOLEAN)
                    && g_variant_get_boolean(isVpn);
            g_variant_unref(isVpn);
        }
    }
    g_variant_unref(active);
    return found;
}

void readDevice(const char* path, guint32 type, NmNet::Device& out)
{
    // Several devices can share a type -- two wifi adapters, or a laptop dock's
    // ethernet beside the built-in one. An activated one always wins; otherwise
    // the first seen is kept, so the state is "disconnected" with a real
    // interface name rather than empty.
    NmNet::Device device;
    device.present = true;
    device.state = static_cast<int>(
        uintProperty(path, "org.freedesktop.NetworkManager.Device", "State", 0));
    device.interfaceName =
        stringProperty(path, "org.freedesktop.NetworkManager.Device", "Interface");

    if (device.state == NmNet::kDeviceActivated)
        device.ipAddress = addressOf(path);

    if (type == NmNet::kDeviceWifi) {
        device.strength = 0;
        const std::string ap = stringProperty(
            path, "org.freedesktop.NetworkManager.Device.Wireless", "ActiveAccessPoint");
        if (!ap.empty() && ap != "/") {
            device.ssid = ssidOf(ap);
            device.strength = static_cast<int>(uintProperty(
                ap.c_str(), "org.freedesktop.NetworkManager.AccessPoint", "Strength", 0));
        }
    }

    if (!out.present || (device.activated() && !out.activated()))
        out = device;
}

NmNet::NetworkState readState()
{
    NmNet::NetworkState state;
    if (!g_system)
        return state;   // no bus: everything disconnected, which is the truth

    state.connectivity = static_cast<int>(
        uintProperty(kNmPath, kNmIface, "Connectivity", NmNet::kConnectivityUnknown));

    GVariant* devices = property(kNmPath, kNmIface, "Devices");
    if (devices) {
        GVariantIter iter;
        const char* path = nullptr;
        g_variant_iter_init(&iter, devices);
        while (g_variant_iter_next(&iter, "&o", &path)) {
            const guint32 type =
                uintProperty(path, "org.freedesktop.NetworkManager.Device", "DeviceType",
                             NmNet::kDeviceUnknown);
            // Everything else this machine reports -- bridge, tun, veth,
            // wifi-p2p, loopback -- is not a transport webOS has any notion of.
            if (type == NmNet::kDeviceWifi)
                readDevice(path, type, state.wifi);
            else if (type == NmNet::kDeviceEthernet)
                readDevice(path, type, state.wired);
        }
        g_variant_unref(devices);
    }

    state.vpnActive = anyVpnActive();
    return state;
}

// --- telling webOS ----------------------------------------------------------

void post(LSPalmService* service, const std::string& payload)
{
    if (!service)
        return;
    LSHandle* const handles[] = {
        LSPalmServiceGetPrivateConnection(service),
        LSPalmServiceGetPublicConnection(service),
    };
    for (LSHandle* handle : handles) {
        if (!handle)
            continue;
        for (const char* const* method = kStatusMethods; *method; ++method) {
            LSError error;
            LSErrorInit(&error);
            if (!LSSubscriptionPost(handle, kCategory, *method, payload.c_str(), &error))
                logAndFree("LSSubscriptionPost", error);
        }
    }
}

void refresh()
{
    g_state = readState();
    // Compared as the payload rather than field by field: if what the
    // subscribers would read has not changed, there is nothing to tell them.
    // NetworkManager emits PropertiesChanged for things webOS has no notion of
    // -- a container's veth appearing, a route metric moving -- and each one
    // would otherwise wake every consumer.
    // The two names are posted independently: the wifi indicator follows states
    // the connectionmanager payload does not distinguish -- joining a network
    // moves through associating and associated while both of its answers still
    // read "disconnected" -- so a shared guard would swallow those updates.
    const std::string payload = NmNet::statusPayload(g_state, true);
    const std::string wifiPayload = NmNet::wifiStatusPayload(g_state, true);
    const std::string wifiKey = NmNet::wifiChangeKey(g_state);
    const bool changed = payload != g_lastPayload;
    const bool wifiChanged = wifiKey != g_lastWifiKey;
    if (!changed && !wifiChanged)
        return;

    if (changed) {
        g_lastPayload = payload;
        g_message("nm-connectionmanager: wifi=%s%s%s wired=%s internet=%s",
                  NmNet::deviceState(g_state.wifi),
                  g_state.wifi.ssid.empty() ? "" : " ",
                  g_state.wifi.ssid.c_str(),
                  NmNet::deviceState(g_state.wired),
                  NmNet::internetAvailable(g_state) ? "yes" : "no");
        post(g_service, payload);
    }
    if (wifiChanged) {
        g_lastWifiKey = wifiKey;
        post(g_wifiService, wifiPayload);
    }
}

gboolean refreshNow(gpointer)
{
    g_refreshPending = 0;
    refresh();
    return G_SOURCE_REMOVE;
}

// Changes arrive in bursts: joining a network moves the device through five
// states and rewrites its address, each as its own signal. One read per burst.
void scheduleRefresh()
{
    if (!g_refreshPending)
        g_refreshPending = g_idle_add(refreshNow, nullptr);
}

void onNmSignal(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                const gchar*, GVariant*, gpointer)
{
    scheduleRefresh();
}

// --- the bus methods --------------------------------------------------------

bool getStatus(LSHandle* sh, LSMessage* message, void*)
{
    bool subscribed = false;
    LSError error;
    LSErrorInit(&error);
    if (!LSSubscriptionProcess(sh, message, &subscribed, &error))
        logAndFree("LSSubscriptionProcess", error);

    const std::string payload = NmNet::statusPayload(g_state, subscribed);
    LSErrorInit(&error);
    if (!LSMessageReply(sh, message, payload.c_str(), &error))
        logAndFree("LSMessageReply", error);
    return true;
}

LSMethod kMethods[] = {
    { "getStatus", getStatus },
    { "getstatus", getStatus },
    { },
};

// The status bar calls getstatus; enyo's wifi library calls it too. Both
// spellings again, for the same reason as above.
bool getWifiStatus(LSHandle* sh, LSMessage* message, void*)
{
    bool subscribed = false;
    LSError error;
    LSErrorInit(&error);
    if (!LSSubscriptionProcess(sh, message, &subscribed, &error))
        logAndFree("LSSubscriptionProcess", error);

    const std::string payload = NmNet::wifiStatusPayload(g_state, subscribed);
    LSErrorInit(&error);
    if (!LSMessageReply(sh, message, payload.c_str(), &error))
        logAndFree("LSMessageReply", error);
    return true;
}

LSMethod kWifiMethods[] = {
    { "getStatus", getWifiStatus },
    { "getstatus", getWifiStatus },
    { },
};

gboolean quit(gpointer)
{
    g_main_loop_quit(g_loop);
    return G_SOURCE_REMOVE;
}

} // namespace

int main()
{
    g_loop = g_main_loop_new(nullptr, FALSE);

    // The system bus, where NetworkManager lives. Reachable from inside the
    // session's namespace: tools/run-lunasysmgr.sh binds the host's /run, and
    // org.freedesktop.NetworkManager answers there exactly as it does outside.
    // If it is not reachable the service still starts and reports a
    // disconnected machine, which is better than the shell losing
    // com.palm.connectionmanager altogether -- that is what the stub was added
    // to prevent in the first place.
    GError* gerror = nullptr;
    g_system = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &gerror);
    if (!g_system) {
        g_warning("nm-connectionmanager: no system bus: %s",
                  gerror ? gerror->message : "(no message)");
        g_clear_error(&gerror);
    }

    LSError error;
    LSErrorInit(&error);
    if (!LSRegisterPalmService(kServiceName, &g_service, &error)) {
        logAndFree("LSRegisterPalmService", error);
        return 1;
    }

    // On both buses, unlike sysfs-powerd: the callers are split across them --
    // the status bar and BrowserServer on one, luna-sysservice and
    // activitymanager on the other -- and HP's stub had a role on each.
    LSErrorInit(&error);
    if (!LSPalmServiceRegisterCategory(g_service, kCategory, kMethods, kMethods,
                                       nullptr, nullptr, &error)) {
        logAndFree("LSPalmServiceRegisterCategory", error);
        return 1;
    }

    LSErrorInit(&error);
    if (!LSGmainAttachPalmService(g_service, g_loop, &error)) {
        logAndFree("LSGmainAttachPalmService", error);
        return 1;
    }

    // com.palm.wifi. Not fatal if it cannot be had: com.palm.connectionmanager
    // is what the apps need to stop believing they are online, and losing the
    // indicator is better than losing both.
    LSErrorInit(&error);
    if (!LSRegisterPalmService(kWifiServiceName, &g_wifiService, &error)) {
        logAndFree("LSRegisterPalmService(com.palm.wifi)", error);
        g_wifiService = nullptr;
    } else {
        LSErrorInit(&error);
        if (!LSPalmServiceRegisterCategory(g_wifiService, kCategory, kWifiMethods,
                                           kWifiMethods, nullptr, nullptr, &error)) {
            logAndFree("LSPalmServiceRegisterCategory(com.palm.wifi)", error);
            g_wifiService = nullptr;
        } else {
            LSErrorInit(&error);
            if (!LSGmainAttachPalmService(g_wifiService, g_loop, &error)) {
                logAndFree("LSGmainAttachPalmService(com.palm.wifi)", error);
                g_wifiService = nullptr;
            }
        }
    }

    if (g_system) {
        // Everything NetworkManager says about itself and its objects. The
        // property signal carries the interface it belongs to, but filtering on
        // it here would mean listing every one that matters -- device, access
        // point, IP config, active connection -- and missing one shows up as a
        // state that stops updating in a specific case. Reading once per burst
        // is cheap enough to not need that risk.
        g_dbus_connection_signal_subscribe(
            g_system, kNmService, "org.freedesktop.DBus.Properties",
            "PropertiesChanged", nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
            onNmSignal, nullptr, nullptr);
        for (const char* member : { "DeviceAdded", "DeviceRemoved", "StateChanged" }) {
            g_dbus_connection_signal_subscribe(
                g_system, kNmService, kNmIface, member, nullptr, nullptr,
                G_DBUS_SIGNAL_FLAGS_NONE, onNmSignal, nullptr, nullptr);
        }
    }

    // The state before anyone can ask for it, and g_lastPayload with it, so the
    // first real change is what gets posted rather than a duplicate of this.
    g_state = readState();
    g_lastPayload = NmNet::statusPayload(g_state, true);
    g_lastWifiKey = NmNet::wifiChangeKey(g_state);
    g_message("nm-connectionmanager: com.palm.connectionmanager up, wifi=%s wired=%s internet=%s",
              NmNet::deviceState(g_state.wifi), NmNet::deviceState(g_state.wired),
              NmNet::internetAvailable(g_state) ? "yes" : "no");

    g_unix_signal_add(SIGTERM, quit, nullptr);
    g_unix_signal_add(SIGINT, quit, nullptr);

    g_main_loop_run(g_loop);

    LSErrorInit(&error);
    if (!LSUnregisterPalmService(g_service, &error))
        logAndFree("LSUnregisterPalmService", error);
    if (g_wifiService) {
        LSErrorInit(&error);
        if (!LSUnregisterPalmService(g_wifiService, &error))
            logAndFree("LSUnregisterPalmService(com.palm.wifi)", error);
    }
    if (g_system)
        g_object_unref(g_system);
    g_main_loop_unref(g_loop);
    return 0;
}
