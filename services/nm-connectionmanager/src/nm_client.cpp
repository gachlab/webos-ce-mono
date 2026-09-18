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

#include "nm_client.h"

namespace NmClient {

namespace {

const char kNmService[] = "org.freedesktop.NetworkManager";
const char kNmPath[] = "/org/freedesktop/NetworkManager";
const char kNmIface[] = "org.freedesktop.NetworkManager";

// One property, or nullptr. A device can disappear between being listed and
// being read -- unplugging a USB adapter, a container going away -- and that is
// ordinary, so a failure here is silent rather than a warning on every change.
GVariant* property(GDBusConnection* bus, const char* path, const char* iface, const char* name)
{
    if (!bus)
        return nullptr;
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        bus, kNmService, path, "org.freedesktop.DBus.Properties", "Get",
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

guint32 uintProperty(GDBusConnection* bus, const char* path, const char* iface, const char* name, guint32 fallback)
{
    GVariant* v = property(bus, path, iface, name);
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

bool boolProperty(GDBusConnection* bus, const char* path, const char* iface, const char* name)
{
    GVariant* v = property(bus, path, iface, name);
    if (!v)
        return false;
    const bool out = g_variant_is_of_type(v, G_VARIANT_TYPE_BOOLEAN)
                     && g_variant_get_boolean(v);
    g_variant_unref(v);
    return out;
}

std::string stringProperty(GDBusConnection* bus, const char* path, const char* iface, const char* name)
{
    GVariant* v = property(bus, path, iface, name);
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
std::string ssidOf(GDBusConnection* bus, const std::string& apPath)
{
    if (apPath.empty() || apPath == "/")
        return std::string();
    GVariant* v = property(bus, apPath.c_str(), "org.freedesktop.NetworkManager.AccessPoint", "Ssid");
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
std::string addressOf(GDBusConnection* bus, const char* devicePath)
{
    const std::string configPath =
        stringProperty(bus, devicePath, "org.freedesktop.NetworkManager.Device", "Ip4Config");
    if (configPath.empty() || configPath == "/")
        return std::string();

    GVariant* data = property(bus, configPath.c_str(),
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

// A boolean that may not exist: fallback when it cannot be read, rather than
// false. WirelessEnabled missing must not read as a radio switched off.
bool optionalBool(GDBusConnection* bus, const char* path, const char* iface,
                  const char* name, bool fallback)
{
    GVariant* v = property(bus, path, iface, name);
    if (!v)
        return fallback;
    const bool out = g_variant_is_of_type(v, G_VARIANT_TYPE_BOOLEAN)
                     ? g_variant_get_boolean(v) : fallback;
    g_variant_unref(v);
    return out;
}

// Device.StateReason is (state, reason); only the reason is wanted.
guint32 stateReasonOf(GDBusConnection* bus, const char* devicePath)
{
    GVariant* v = property(bus, devicePath, "org.freedesktop.NetworkManager.Device", "StateReason");
    if (!v)
        return 0;
    guint32 state = 0, reason = 0;
    if (g_variant_is_of_type(v, G_VARIANT_TYPE("(uu)")))
        g_variant_get(v, "(uu)", &state, &reason);
    g_variant_unref(v);
    return reason;
}

// The saved profile behind a device's active connection, 0 when none.
int activeProfileId(GDBusConnection* bus, const char* devicePath)
{
    const std::string active =
        stringProperty(bus, devicePath, "org.freedesktop.NetworkManager.Device", "ActiveConnection");
    if (active.empty() || active == "/")
        return 0;
    return NmNet::profileIdOf(stringProperty(
        bus, active.c_str(), "org.freedesktop.NetworkManager.Connection.Active", "Connection"));
}

// Whether any active connection is a VPN. NM's PrimaryConnection is the tunnel
// itself while one is up -- measured, with the primary connection reading
// type "vpn" over a wifi that was carrying it -- which is why the transport is
// found by walking the devices instead of trusting the primary connection.
bool anyVpnActive(GDBusConnection* bus)
{
    GVariant* active = property(bus, kNmPath, kNmIface, "ActiveConnections");
    if (!active)
        return false;

    bool found = false;
    GVariantIter iter;
    const char* path = nullptr;
    g_variant_iter_init(&iter, active);
    while (!found && g_variant_iter_next(&iter, "&o", &path)) {
        GVariant* isVpn = property(bus, path, "org.freedesktop.NetworkManager.Connection.Active", "Vpn");
        if (isVpn) {
            found = g_variant_is_of_type(isVpn, G_VARIANT_TYPE_BOOLEAN)
                    && g_variant_get_boolean(isVpn);
            g_variant_unref(isVpn);
        }
    }
    g_variant_unref(active);
    return found;
}

// The ethernet device's object path, for the calls that change its state. The
// first one found: a machine with two sockets is a machine where either will do
// as "the cable", and nothing in webOS can express a choice between them anyway.
std::string wiredDevicePath(GDBusConnection* bus)
{
    if (!bus)
        return std::string();
    GVariant* devices = property(bus, kNmPath, kNmIface, "Devices");
    if (!devices)
        return std::string();
    std::string found;
    GVariantIter iter;
    const char* path = nullptr;
    g_variant_iter_init(&iter, devices);
    while (found.empty() && g_variant_iter_next(&iter, "&o", &path)) {
        if (uintProperty(bus, path, "org.freedesktop.NetworkManager.Device", "DeviceType",
                         NmNet::kDeviceUnknown) == NmNet::kDeviceEthernet)
            found = path;
    }
    g_variant_unref(devices);
    return found;
}

// Bringing the cable up is not the mirror image of taking it down.
//
// Down is Device.Disconnect, which also tells NetworkManager not to bring it
// back by itself. Up has two cases: normally there is a saved connection for the
// device and ActivateConnection uses it, but a socket that has never been
// configured has none -- measured, AvailableConnections was empty -- and then
// AddAndActivateConnection with empty settings makes NM build the default DHCP
// profile, which is what it would have done unprompted.
bool activateWired(GDBusConnection* bus, const std::string& device, std::string& error)
{
    GVariant* available = property(bus, device.c_str(),
                                   "org.freedesktop.NetworkManager.Device",
                                   "AvailableConnections");
    std::string connection;
    if (available) {
        GVariantIter iter;
        const char* path = nullptr;
        g_variant_iter_init(&iter, available);
        if (g_variant_iter_next(&iter, "&o", &path))
            connection = path;
        g_variant_unref(available);
    }

    GError* gerror = nullptr;
    GVariant* reply = nullptr;
    if (!connection.empty()) {
        reply = g_dbus_connection_call_sync(
            bus, kNmService, kNmPath, kNmIface, "ActivateConnection",
            g_variant_new("(ooo)", connection.c_str(), device.c_str(), "/"),
            nullptr, G_DBUS_CALL_FLAGS_NONE, 8000, nullptr, &gerror);
    } else {
        GVariantBuilder settings;
        g_variant_builder_init(&settings, G_VARIANT_TYPE("a{sa{sv}}"));
        reply = g_dbus_connection_call_sync(
            bus, kNmService, kNmPath, kNmIface, "AddAndActivateConnection",
            g_variant_new("(a{sa{sv}}oo)", &settings, device.c_str(), "/"),
            nullptr, G_DBUS_CALL_FLAGS_NONE, 8000, nullptr, &gerror);
    }
    if (!reply) {
        error = gerror && gerror->message ? gerror->message : "no reply";
        g_clear_error(&gerror);
        return false;
    }
    g_variant_unref(reply);
    return true;
}

bool deactivateWired(GDBusConnection* bus, const std::string& device, std::string& error)
{
    GError* gerror = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        bus, kNmService, device.c_str(), "org.freedesktop.NetworkManager.Device",
        "Disconnect", nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, 8000, nullptr, &gerror);
    if (!reply) {
        error = gerror && gerror->message ? gerror->message : "no reply";
        g_clear_error(&gerror);
        return false;
    }
    g_variant_unref(reply);
    return true;
}

bool readDevice(GDBusConnection* bus, const char* path, guint32 type, NmNet::Device& out)
{
    // Several devices can share a type -- two wifi adapters, or a laptop dock's
    // ethernet beside the built-in one. An activated one always wins; otherwise
    // the first seen is kept, so the state is "disconnected" with a real
    // interface name rather than empty.
    NmNet::Device device;
    device.present = true;
    device.state = static_cast<int>(
        uintProperty(bus, path, "org.freedesktop.NetworkManager.Device", "State", 0));
    device.interfaceName =
        stringProperty(bus, path, "org.freedesktop.NetworkManager.Device", "Interface");

    if (device.state == NmNet::kDeviceActivated)
        device.ipAddress = addressOf(bus, path);

    // Whether the cable is in, which is not what State says: an unplugged
    // socket and a socket whose connection was taken down both read
    // disconnected, and only one of them can be connected again.
    if (type == NmNet::kDeviceEthernet)
        device.carrier = boolProperty(bus, path, "org.freedesktop.NetworkManager.Device.Wired", "Carrier");

    if (type == NmNet::kDeviceWifi) {
        device.strength = 0;
        const std::string ap = stringProperty(
            bus, path, "org.freedesktop.NetworkManager.Device.Wireless", "ActiveAccessPoint");
        if (!ap.empty() && ap != "/") {
            device.ssid = ssidOf(bus, ap);
            device.strength = static_cast<int>(uintProperty(
                bus, ap.c_str(), "org.freedesktop.NetworkManager.AccessPoint", "Strength", 0));
        }
    }

    if (!out.present || (device.activated() && !out.activated())) {
        out = device;
        return true;
    }
    return false;
}

} // namespace

NmNet::NetworkState readState(GDBusConnection* bus)
{
    NmNet::NetworkState state;
    if (!bus)
        return state;   // no bus: everything disconnected, which is the truth

    state.connectivity = static_cast<int>(
        uintProperty(bus, kNmPath, kNmIface, "Connectivity", NmNet::kConnectivityUnknown));

    std::string wifiPath;
    GVariant* devices = property(bus, kNmPath, kNmIface, "Devices");
    if (devices) {
        GVariantIter iter;
        const char* path = nullptr;
        g_variant_iter_init(&iter, devices);
        while (g_variant_iter_next(&iter, "&o", &path)) {
            const guint32 type =
                uintProperty(bus, path, "org.freedesktop.NetworkManager.Device", "DeviceType",
                             NmNet::kDeviceUnknown);
            // Everything else this machine reports -- bridge, tun, veth,
            // wifi-p2p, loopback -- is not a transport webOS has any notion of.
            if (type == NmNet::kDeviceWifi) {
                if (readDevice(bus, path, type, state.wifi))
                    wifiPath = path;
            }
            else if (type == NmNet::kDeviceEthernet)
                readDevice(bus, path, type, state.wired);
        }
        g_variant_unref(devices);
    }

    state.vpnActive = anyVpnActive(bus);
    state.wifiEnabled = optionalBool(bus, kNmPath, kNmIface, "WirelessEnabled", true);
    if (!wifiPath.empty()) {
        state.wifiStateReason = static_cast<int>(stateReasonOf(bus, wifiPath.c_str()));
        state.wifiProfileId = activeProfileId(bus, wifiPath.c_str());
        const std::string ap = stringProperty(bus, wifiPath.c_str(),
                                              "org.freedesktop.NetworkManager.Device.Wireless",
                                              "ActiveAccessPoint");
        if (!ap.empty() && ap != "/") {
            const char* apIface = "org.freedesktop.NetworkManager.AccessPoint";
            state.wifiBssid = stringProperty(bus, ap.c_str(), apIface, "HwAddress");
            state.wifiFrequency = static_cast<int>(uintProperty(bus, ap.c_str(), apIface, "Frequency", 0));
        }
    }
    return state;
}

bool setWired(GDBusConnection* bus, bool connected, std::string& error)
{
    const std::string device = wiredDevicePath(bus);
    if (device.empty()) {
        error = "no wired device";
        return false;
    }
    return connected ? activateWired(bus, device, error)
                     : deactivateWired(bus, device, error);
}


// --- wifi -------------------------------------------------------------------

namespace {

const char kDeviceIface[] = "org.freedesktop.NetworkManager.Device";
const char kWirelessIface[] = "org.freedesktop.NetworkManager.Device.Wireless";
const char kApIface[] = "org.freedesktop.NetworkManager.AccessPoint";
const char kSettingsPath[] = "/org/freedesktop/NetworkManager/Settings";
const char kSettingsIface[] = "org.freedesktop.NetworkManager.Settings";
const char kConnectionIface[] = "org.freedesktop.NetworkManager.Settings.Connection";

// A call whose failure is reported with NetworkManager's message.
GVariant* call(GDBusConnection* bus, const char* path, const char* iface, const char* method,
               GVariant* args, const GVariantType* replyType, std::string& error)
{
    GError* gerror = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        bus, kNmService, path, iface, method, args, replyType,
        G_DBUS_CALL_FLAGS_NONE, 8000, nullptr, &gerror);
    if (!reply) {
        error = gerror && gerror->message ? gerror->message : "no reply";
        g_clear_error(&gerror);
    }
    return reply;
}

// The wifi device, chosen as readState chooses it: an activated one, else the
// first.
std::string wifiDevicePath(GDBusConnection* bus)
{
    if (!bus)
        return std::string();
    GVariant* devices = property(bus, kNmPath, kNmIface, "Devices");
    if (!devices)
        return std::string();
    std::string first, activated;
    GVariantIter iter;
    const char* path = nullptr;
    g_variant_iter_init(&iter, devices);
    while (activated.empty() && g_variant_iter_next(&iter, "&o", &path)) {
        if (uintProperty(bus, path, kDeviceIface, "DeviceType", NmNet::kDeviceUnknown)
            != NmNet::kDeviceWifi)
            continue;
        if (first.empty())
            first = path;
        if (uintProperty(bus, path, kDeviceIface, "State", 0) == NmNet::kDeviceActivated)
            activated = path;
    }
    g_variant_unref(devices);
    return activated.empty() ? first : activated;
}

std::string bytesOf(GVariant* v)
{
    std::string out;
    if (v && g_variant_is_of_type(v, G_VARIANT_TYPE_BYTESTRING)) {
        gsize length = 0;
        const guchar* bytes = static_cast<const guchar*>(
            g_variant_get_fixed_array(v, &length, sizeof(guchar)));
        if (bytes && length)
            out.assign(reinterpret_cast<const char*>(bytes), length);
    }
    return out;
}

GVariant* bytesVariant(const std::string& s)
{
    return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, s.data(), s.size(), 1);
}

// A connection's settings, a{sa{sv}}, or nullptr.
GVariant* settingsOf(GDBusConnection* bus, const std::string& path, std::string& error)
{
    GVariant* reply = call(bus, path.c_str(), kConnectionIface, "GetSettings", nullptr,
                           G_VARIANT_TYPE("(a{sa{sv}})"), error);
    if (!reply)
        return nullptr;
    GVariant* settings = g_variant_get_child_value(reply, 0);
    g_variant_unref(reply);
    return settings;
}

// One key of one group of a settings dictionary, or nullptr.
GVariant* settingOf(GVariant* settings, const char* group, const char* key)
{
    GVariant* groupDict = g_variant_lookup_value(settings, group, G_VARIANT_TYPE("a{sv}"));
    if (!groupDict)
        return nullptr;
    GVariant* value = g_variant_lookup_value(groupDict, key, nullptr);
    g_variant_unref(groupDict);
    return value;
}

std::string stringSettingOf(GVariant* settings, const char* group, const char* key)
{
    GVariant* v = settingOf(settings, group, key);
    std::string out;
    if (v && g_variant_is_of_type(v, G_VARIANT_TYPE_STRING))
        out = g_variant_get_string(v, nullptr);
    if (v)
        g_variant_unref(v);
    return out;
}

bool isWifiProfile(GVariant* settings)
{
    return stringSettingOf(settings, "connection", "type") == "802-11-wireless";
}

NmNet::Security securityOfSettings(GVariant* settings)
{
    const std::string keyMgmt = stringSettingOf(settings, "802-11-wireless-security", "key-mgmt");
    if (keyMgmt == "wpa-psk")
        return NmNet::kSecurityWpaPsk;
    if (keyMgmt == "sae")
        return NmNet::kSecuritySae;
    if (keyMgmt == "none")
        return NmNet::kSecurityWep;
    if (keyMgmt == "wpa-eap" || keyMgmt == "ieee8021x" || keyMgmt == "wpa-eap-suite-b-192")
        return NmNet::kSecurityEnterprise;
    return NmNet::kSecurityNone;
}

struct SavedWifi {
    std::string path;
    std::string ssid;
};

// Every saved wifi profile. Only the name is kept: GetSettings never returns
// secrets, and nothing here needs them.
std::vector<SavedWifi> savedWifiProfiles(GDBusConnection* bus)
{
    std::vector<SavedWifi> out;
    std::string error;
    GVariant* reply = call(bus, kSettingsPath, kSettingsIface, "ListConnections", nullptr,
                           G_VARIANT_TYPE("(ao)"), error);
    if (!reply)
        return out;
    GVariantIter* iter = nullptr;
    const char* path = nullptr;
    g_variant_get(reply, "(ao)", &iter);
    while (g_variant_iter_next(iter, "&o", &path)) {
        GVariant* settings = settingsOf(bus, path, error);
        if (!settings)
            continue;
        if (isWifiProfile(settings)) {
            GVariant* ssid = settingOf(settings, "802-11-wireless", "ssid");
            out.push_back({ path, bytesOf(ssid) });
            if (ssid)
                g_variant_unref(ssid);
        }
        g_variant_unref(settings);
    }
    g_variant_iter_free(iter);
    g_variant_unref(reply);
    return out;
}

std::string savedPathFor(const std::vector<SavedWifi>& saved, const std::string& ssid)
{
    for (const SavedWifi& s : saved)
        if (s.ssid == ssid)
            return s.path;
    return std::string();
}

struct ScannedAp {
    std::string path;
    NmNet::AccessPoint ap;
};

std::vector<ScannedAp> accessPoints(GDBusConnection* bus, const std::string& device)
{
    std::vector<ScannedAp> out;
    GVariant* aps = property(bus, device.c_str(), kWirelessIface, "AccessPoints");
    if (!aps)
        return out;
    const std::string activeAp = stringProperty(bus, device.c_str(), kWirelessIface, "ActiveAccessPoint");
    GVariantIter iter;
    const char* path = nullptr;
    g_variant_iter_init(&iter, aps);
    while (g_variant_iter_next(&iter, "&o", &path)) {
        ScannedAp scanned;
        scanned.path = path;
        GVariant* ssid = property(bus, path, kApIface, "Ssid");
        scanned.ap.ssid = bytesOf(ssid);
        if (ssid)
            g_variant_unref(ssid);
        scanned.ap.strength = static_cast<int>(uintProperty(bus, path, kApIface, "Strength", 0));
        scanned.ap.security = NmNet::apSecurity(uintProperty(bus, path, kApIface, "Flags", 0),
                                                uintProperty(bus, path, kApIface, "WpaFlags", 0),
                                                uintProperty(bus, path, kApIface, "RsnFlags", 0));
        scanned.ap.active = activeAp == path;
        out.push_back(scanned);
    }
    g_variant_unref(aps);
    return out;
}

// The group NetworkManager wants for the security a join asks for, or nullptr
// for an open network.
GVariant* securityGroup(NmNet::Security security, const NmNet::ConnectRequest& request)
{
    const char* keyMgmt = NmNet::keyManagement(security);
    if (!keyMgmt)
        return nullptr;
    GVariantBuilder group;
    g_variant_builder_init(&group, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&group, "{sv}", "key-mgmt", g_variant_new_string(keyMgmt));
    if (security == NmNet::kSecurityWep) {
        // NM_WEP_KEY_TYPE_KEY takes both forms a WEP key comes in: 5 or 13
        // characters, or 10 or 26 hex digits.
        const std::string slot = "wep-key" + std::to_string(request.keyIndex);
        g_variant_builder_add(&group, "{sv}", slot.c_str(),
                              g_variant_new_string(request.passKey.c_str()));
        g_variant_builder_add(&group, "{sv}", "wep-tx-keyidx",
                              g_variant_new_uint32(static_cast<guint32>(request.keyIndex)));
        g_variant_builder_add(&group, "{sv}", "wep-key-type", g_variant_new_uint32(1));
    } else if (security != NmNet::kSecurityEnterprise) {
        g_variant_builder_add(&group, "{sv}", "psk", g_variant_new_string(request.passKey.c_str()));
    }
    return g_variant_builder_end(&group);
}

// A certificate or key property: NetworkManager takes a path as the bytes of a
// file:// URI with its terminating NUL.
GVariant* pathBlob(const std::string& path)
{
    std::string uri = "file://" + path;
    uri.push_back('\0');
    return bytesVariant(uri);
}

// The 802-1x group of an enterprise join, or nullptr for any other.
//
// PEAP, TTLS and FAST log in with a user name and password, checked inside the
// tunnel with MSCHAPv2, which is what every enterprise network the settings
// card was made for used; "Auto" offers PEAP and TTLS and lets the server pick.
// TLS logs in with a certificate, whose file carries its key too. Checking the
// server's certificate means checking it against the system's CAs.
GVariant* eapGroup(NmNet::Security security, const NmNet::ConnectRequest& request)
{
    if (security != NmNet::kSecurityEnterprise)
        return nullptr;
    GVariantBuilder group;
    g_variant_builder_init(&group, G_VARIANT_TYPE("a{sv}"));
    GVariantBuilder methods;
    g_variant_builder_init(&methods, G_VARIANT_TYPE("as"));
    for (const std::string& method : NmNet::eapMethods(request.eapType))
        g_variant_builder_add(&methods, "s", method.c_str());
    g_variant_builder_add(&group, "{sv}", "eap", g_variant_builder_end(&methods));
    g_variant_builder_add(&group, "{sv}", "identity", g_variant_new_string(request.userId.c_str()));
    if (request.eapType == "eapTls") {
        g_variant_builder_add(&group, "{sv}", "client-cert", pathBlob(request.clientCertificatePath));
        g_variant_builder_add(&group, "{sv}", "private-key", pathBlob(request.clientCertificatePath));
        // NM_SETTING_SECRET_FLAG_NOT_REQUIRED: the key in these files is not
        // encrypted, and the card has nowhere to ask for a key password.
        g_variant_builder_add(&group, "{sv}", "private-key-password-flags", g_variant_new_uint32(4));
    } else {
        g_variant_builder_add(&group, "{sv}", "password", g_variant_new_string(request.password.c_str()));
        g_variant_builder_add(&group, "{sv}", "phase2-auth", g_variant_new_string("mschapv2"));
        if (request.eapType == "eapFast")
            g_variant_builder_add(&group, "{sv}", "phase1-fast-provisioning", g_variant_new_string("3"));
    }
    if (request.verifyServerCert)
        g_variant_builder_add(&group, "{sv}", "system-ca-certs", g_variant_new_boolean(TRUE));
    return g_variant_builder_end(&group);
}

// An IPv4 address as NetworkManager's "dns" wants it: network byte order.
guint32 networkOrder(const std::string& address)
{
    unsigned long value = 0;
    NmNet::parseIpv4(address, value);
    return g_htonl(static_cast<guint32>(value));
}

// The ipv4 group for the address settings screen: DHCP, or the addresses given.
GVariant* ipv4Group(const NmNet::ConnectRequest& request)
{
    GVariantBuilder group;
    g_variant_builder_init(&group, G_VARIANT_TYPE("a{sv}"));
    if (!request.staticIp) {
        g_variant_builder_add(&group, "{sv}", "method", g_variant_new_string("auto"));
        return g_variant_builder_end(&group);
    }
    g_variant_builder_add(&group, "{sv}", "method", g_variant_new_string("manual"));
    GVariantBuilder addresses;
    g_variant_builder_init(&addresses, G_VARIANT_TYPE("aa{sv}"));
    GVariantBuilder address;
    g_variant_builder_init(&address, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&address, "{sv}", "address", g_variant_new_string(request.ip.c_str()));
    g_variant_builder_add(&address, "{sv}", "prefix",
                          g_variant_new_uint32(static_cast<guint32>(NmNet::prefixOfMask(request.subnet))));
    g_variant_builder_add(&addresses, "a{sv}", &address);
    g_variant_builder_add(&group, "{sv}", "address-data", g_variant_builder_end(&addresses));
    if (!request.gateway.empty())
        g_variant_builder_add(&group, "{sv}", "gateway", g_variant_new_string(request.gateway.c_str()));
    GVariantBuilder dns;
    g_variant_builder_init(&dns, G_VARIANT_TYPE("au"));
    for (const std::string* server : { &request.dns1, &request.dns2 })
        if (!server->empty())
            g_variant_builder_add(&dns, "u", networkOrder(*server));
    g_variant_builder_add(&group, "{sv}", "dns", g_variant_builder_end(&dns));
    return g_variant_builder_end(&group);
}

// A profile's settings with the named groups replaced by the ones given (a null
// group is only removed), everything else kept.
GVariant* replaceGroups(GVariant* settings,
                        std::initializer_list<std::pair<const char*, GVariant*>> groups)
{
    GVariantBuilder all;
    g_variant_builder_init(&all, G_VARIANT_TYPE("a{sa{sv}}"));
    GVariantIter iter;
    const char* group = nullptr;
    GVariant* values = nullptr;
    g_variant_iter_init(&iter, settings);
    while (g_variant_iter_next(&iter, "{&s@a{sv}}", &group, &values)) {
        bool replaced = false;
        for (const auto& g : groups)
            replaced = replaced || std::string(g.first) == group;
        if (!replaced)
            g_variant_builder_add(&all, "{s@a{sv}}", group, values);
        g_variant_unref(values);
    }
    for (const auto& g : groups)
        if (g.second)
            g_variant_builder_add(&all, "{s@a{sv}}", g.first, g.second);
    return g_variant_builder_end(&all);
}

// The settings of a new profile.
GVariant* newWifiSettings(const NmNet::ConnectRequest& request, NmNet::Security security)
{
    GVariantBuilder all;
    g_variant_builder_init(&all, G_VARIANT_TYPE("a{sa{sv}}"));

    GVariantBuilder connection;
    g_variant_builder_init(&connection, G_VARIANT_TYPE("a{sv}"));
    // The id is a display name and has to be valid UTF-8; the ssid below keeps
    // the real bytes.
    gchar* id = g_utf8_make_valid(request.ssid.data(), static_cast<gssize>(request.ssid.size()));
    g_variant_builder_add(&connection, "{sv}", "id", g_variant_new_string(id));
    g_free(id);
    g_variant_builder_add(&connection, "{sv}", "type", g_variant_new_string("802-11-wireless"));
    g_variant_builder_add(&all, "{s@a{sv}}", "connection", g_variant_builder_end(&connection));

    GVariantBuilder wireless;
    g_variant_builder_init(&wireless, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&wireless, "{sv}", "ssid", bytesVariant(request.ssid));
    g_variant_builder_add(&wireless, "{sv}", "mode", g_variant_new_string("infrastructure"));
    if (request.hidden)
        g_variant_builder_add(&wireless, "{sv}", "hidden", g_variant_new_boolean(TRUE));
    g_variant_builder_add(&all, "{s@a{sv}}", "802-11-wireless", g_variant_builder_end(&wireless));

    if (GVariant* sec = securityGroup(security, request))
        g_variant_builder_add(&all, "{s@a{sv}}", "802-11-wireless-security", sec);
    if (GVariant* eap = eapGroup(security, request))
        g_variant_builder_add(&all, "{s@a{sv}}", "802-1x", eap);
    return g_variant_builder_end(&all);
}

// An existing profile's settings with its security replaced, everything else --
// the address settings someone made in GNOME, the autoconnect choice -- kept.
GVariant* withSecurity(GVariant* settings, NmNet::Security security,
                       const NmNet::ConnectRequest& request)
{
    return replaceGroups(settings, {
        { "802-11-wireless-security", securityGroup(security, request) },
        { "802-1x", eapGroup(security, request) },
    });
}

bool update(GDBusConnection* bus, const std::string& path, GVariant* settings, std::string& error)
{
    GVariant* reply = call(bus, path.c_str(), kConnectionIface, "Update",
                           g_variant_new_tuple(&settings, 1), nullptr, error);
    if (!reply)
        return false;
    g_variant_unref(reply);
    return true;
}

bool activate(GDBusConnection* bus, const std::string& connection, const std::string& device,
              const std::string& specific, std::string& error)
{
    GVariant* reply = call(bus, kNmPath, kNmIface, "ActivateConnection",
                           g_variant_new("(ooo)", connection.c_str(), device.c_str(), specific.c_str()),
                           nullptr, error);
    if (!reply)
        return false;
    g_variant_unref(reply);
    return true;
}

// A profile by id, refused unless it is a wifi one.
GVariant* wifiSettingsOf(GDBusConnection* bus, int profileId, std::string& error)
{
    if (profileId <= 0) {
        error = "no such profile";
        return nullptr;
    }
    GVariant* settings = settingsOf(bus, NmNet::settingsPathOf(profileId), error);
    if (!settings)
        return nullptr;
    if (!isWifiProfile(settings)) {
        g_variant_unref(settings);
        error = "not a wifi profile";
        return nullptr;
    }
    return settings;
}

} // namespace

bool setWifiEnabled(GDBusConnection* bus, bool enabled, std::string& error)
{
    if (!bus) {
        error = "no system bus";
        return false;
    }
    GVariant* reply = call(bus, kNmPath, "org.freedesktop.DBus.Properties", "Set",
                           g_variant_new("(ssv)", kNmIface, "WirelessEnabled",
                                         g_variant_new_boolean(enabled)),
                           nullptr, error);
    if (!reply)
        return false;
    g_variant_unref(reply);
    return true;
}

bool scan(GDBusConnection* bus, std::vector<NmNet::AccessPoint>& networks, std::string& error)
{
    const std::string device = wifiDevicePath(bus);
    if (device.empty()) {
        error = "no wifi device";
        return false;
    }
    // Refused while a scan is running or when the last one was moments ago;
    // either way the cached list is what there is to answer with.
    std::string ignored;
    GVariant* reply = call(bus, device.c_str(), kWirelessIface, "RequestScan",
                           g_variant_new("(a{sv})", nullptr), nullptr, ignored);
    if (reply)
        g_variant_unref(reply);

    const std::vector<SavedWifi> saved = savedWifiProfiles(bus);
    std::vector<NmNet::AccessPoint> raw;
    for (ScannedAp& scanned : accessPoints(bus, device)) {
        scanned.ap.profileId = NmNet::profileIdOf(savedPathFor(saved, scanned.ap.ssid));
        raw.push_back(scanned.ap);
    }
    networks = NmNet::mergeScan(raw);
    return true;
}

bool connectWifi(GDBusConnection* bus, const NmNet::ConnectRequest& request,
                 int& profileId, std::string& error)
{
    const std::string device = wifiDevicePath(bus);
    if (device.empty()) {
        error = "no wifi device";
        return false;
    }

    if (request.profileId > 0) {
        GVariant* settings = wifiSettingsOf(bus, request.profileId, error);
        if (!settings)
            return false;
        // The address settings screen: the profile's ipv4 replaced, then the
        // profile brought up again so the new addresses apply.
        if (request.addressChange) {
            GVariant* updated = replaceGroups(settings, { { "ipv4", ipv4Group(request) } });
            g_variant_unref(settings);
            if (!update(bus, NmNet::settingsPathOf(request.profileId), updated, error))
                return false;
        } else {
            g_variant_unref(settings);
        }
        if (!activate(bus, NmNet::settingsPathOf(request.profileId), device, "/", error))
            return false;
        profileId = request.profileId;
        return true;
    }

    // The strongest access point by that name: what it advertises decides
    // between PSK and SAE, and naming it saves NetworkManager a lookup.
    std::string apPath = "/";
    NmNet::Security advertised = NmNet::kSecurityNone;
    bool seen = false;
    int best = -1;
    for (const ScannedAp& scanned : accessPoints(bus, device)) {
        if (scanned.ap.ssid != request.ssid || scanned.ap.strength <= best)
            continue;
        best = scanned.ap.strength;
        apPath = scanned.path;
        advertised = scanned.ap.security;
        seen = true;
    }
    const bool open = request.securityType.empty() || request.securityType == "none";
    if (seen && open && advertised != NmNet::kSecurityNone) {
        error = "this network needs a password";
        return false;
    }
    if (request.hidden)
        apPath = "/";
    const NmNet::Security security = NmNet::requestedSecurity(request, advertised);

    const std::string existing = savedPathFor(savedWifiProfiles(bus), request.ssid);
    if (!existing.empty()) {
        if (!open) {
            GVariant* settings = settingsOf(bus, existing, error);
            if (!settings)
                return false;
            GVariant* updated = withSecurity(settings, security, request);
            g_variant_unref(settings);
            if (!update(bus, existing, updated, error))
                return false;
        }
        if (!activate(bus, existing, device, apPath, error))
            return false;
        profileId = NmNet::profileIdOf(existing);
        return true;
    }

    GVariant* reply = call(bus, kNmPath, kNmIface, "AddAndActivateConnection",
                           g_variant_new("(@a{sa{sv}}oo)", newWifiSettings(request, security),
                                         device.c_str(), apPath.c_str()),
                           G_VARIANT_TYPE("(oo)"), error);
    if (!reply)
        return false;
    const char* created = nullptr;
    const char* active = nullptr;
    g_variant_get(reply, "(&o&o)", &created, &active);
    profileId = NmNet::profileIdOf(created);
    g_variant_unref(reply);
    return true;
}

bool getProfile(GDBusConnection* bus, int profileId, NmNet::Profile& profile,
                NmNet::IpInfo& ip, bool& active, std::string& error)
{
    GVariant* settings = wifiSettingsOf(bus, profileId, error);
    if (!settings)
        return false;
    profile = NmNet::Profile();
    profile.profileId = profileId;
    GVariant* ssid = settingOf(settings, "802-11-wireless", "ssid");
    profile.ssid = bytesOf(ssid);
    if (ssid)
        g_variant_unref(ssid);
    profile.security = securityOfSettings(settings);
    profile.staticIp = stringSettingOf(settings, "ipv4", "method") == "manual";
    g_variant_unref(settings);

    active = false;
    ip = NmNet::IpInfo();
    const std::string device = wifiDevicePath(bus);
    if (device.empty() || activeProfileId(bus, device.c_str()) != profileId)
        return true;
    const std::string config = stringProperty(bus, device.c_str(), kDeviceIface, "Ip4Config");
    if (config.empty() || config == "/")
        return true;
    active = true;
    const char* ip4 = "org.freedesktop.NetworkManager.IP4Config";

    GVariant* data = property(bus, config.c_str(), ip4, "AddressData");
    if (data && g_variant_n_children(data) > 0) {
        GVariant* first = g_variant_get_child_value(data, 0);
        GVariant* address = g_variant_lookup_value(first, "address", G_VARIANT_TYPE_STRING);
        GVariant* prefix = g_variant_lookup_value(first, "prefix", G_VARIANT_TYPE_UINT32);
        if (address)
            ip.ip = g_variant_get_string(address, nullptr);
        if (prefix)
            ip.subnet = NmNet::subnetMask(static_cast<int>(g_variant_get_uint32(prefix)));
        if (address)
            g_variant_unref(address);
        if (prefix)
            g_variant_unref(prefix);
        g_variant_unref(first);
    }
    if (data)
        g_variant_unref(data);
    ip.gateway = stringProperty(bus, config.c_str(), ip4, "Gateway");

    GVariant* dns = property(bus, config.c_str(), ip4, "NameserverData");
    if (dns) {
        for (gsize i = 0; i < g_variant_n_children(dns) && i < 2; ++i) {
            GVariant* entry = g_variant_get_child_value(dns, i);
            GVariant* address = g_variant_lookup_value(entry, "address", G_VARIANT_TYPE_STRING);
            if (address) {
                (i == 0 ? ip.dns1 : ip.dns2) = g_variant_get_string(address, nullptr);
                g_variant_unref(address);
            }
            g_variant_unref(entry);
        }
        g_variant_unref(dns);
    }
    return true;
}

bool deleteProfile(GDBusConnection* bus, int profileId, std::string& error)
{
    GVariant* settings = wifiSettingsOf(bus, profileId, error);
    if (!settings)
        return false;
    g_variant_unref(settings);
    GVariant* reply = call(bus, NmNet::settingsPathOf(profileId).c_str(), kConnectionIface,
                           "Delete", nullptr, nullptr, error);
    if (!reply)
        return false;
    g_variant_unref(reply);
    return true;
}

bool listProfiles(GDBusConnection* bus, std::vector<NmNet::Profile>& profiles, std::string& error)
{
    if (!bus) {
        error = "no system bus";
        return false;
    }
    profiles.clear();
    for (const SavedWifi& saved : savedWifiProfiles(bus)) {
        GVariant* settings = settingsOf(bus, saved.path, error);
        if (!settings)
            continue;
        NmNet::Profile profile;
        profile.profileId = NmNet::profileIdOf(saved.path);
        profile.ssid = saved.ssid;
        profile.security = securityOfSettings(settings);
        profile.staticIp = stringSettingOf(settings, "ipv4", "method") == "manual";
        profiles.push_back(profile);
        g_variant_unref(settings);
    }
    return true;
}

bool wifiMacAddress(GDBusConnection* bus, std::string& mac, std::string& error)
{
    const std::string device = wifiDevicePath(bus);
    if (device.empty()) {
        error = "no wifi device";
        return false;
    }
    // On Device since NetworkManager 1.24; on Device.Wireless before that.
    mac = stringProperty(bus, device.c_str(), kDeviceIface, "HwAddress");
    if (mac.empty())
        mac = stringProperty(bus, device.c_str(), kWirelessIface, "HwAddress");
    return true;
}

// --- vpn --------------------------------------------------------------------

namespace {

const char kActiveIface[] = "org.freedesktop.NetworkManager.Connection.Active";

// A string from vpn.data (a{ss}), which is how OpenVPN stores remote/username.
std::string vpnDataOf(GVariant* settings, const char* key)
{
    GVariant* data = settingOf(settings, "vpn", "data");
    if (!data)
        return std::string();
    std::string out;
    if (g_variant_is_of_type(data, G_VARIANT_TYPE("a{ss}"))) {
        const char* value = nullptr;
        if (g_variant_lookup(data, key, "&s", &value) && value)
            out = value;
    } else if (g_variant_is_of_type(data, G_VARIANT_TYPE("a{sv}"))) {
        GVariant* v = g_variant_lookup_value(data, key, G_VARIANT_TYPE_STRING);
        if (v) {
            out = g_variant_get_string(v, nullptr);
            g_variant_unref(v);
        }
    }
    g_variant_unref(data);
    return out;
}

bool isVpnProfile(GVariant* settings)
{
    const std::string type = stringSettingOf(settings, "connection", "type");
    if (type == "wireguard")
        return true;
    if (type != "vpn")
        return false;
    const std::string service = stringSettingOf(settings, "vpn", "service-type");
    // Only the agents this port configures. A Cisco profile from the host stays
    // out of the card rather than being rewritten with the wrong form.
    return service.empty() || service == NmNet::kNmOpenVpnService;
}

std::string agentOfSettings(GVariant* settings)
{
    if (stringSettingOf(settings, "connection", "type") == "wireguard")
        return NmNet::kVpnAgentWireGuard;
    return NmNet::kVpnAgentOpenVpn;
}

// NM_ACTIVE_CONNECTION_STATE_*: 1 activating, 2 activated, 3 deactivating, 4 deactivated.
std::string connectStateOf(guint32 state)
{
    switch (state) {
    case 1: return NmNet::kVpnStateConnecting;
    case 2: return NmNet::kVpnStateConnected;
    case 3: return NmNet::kVpnStateDisconnecting;
    default: return NmNet::kVpnStateDisconnected;
    }
}

// The active-connection path for a settings path, or empty.
std::string activePathFor(GDBusConnection* bus, const std::string& settingsPath)
{
    GVariant* active = property(bus, kNmPath, kNmIface, "ActiveConnections");
    if (!active)
        return std::string();
    std::string found;
    GVariantIter iter;
    const char* path = nullptr;
    g_variant_iter_init(&iter, active);
    while (found.empty() && g_variant_iter_next(&iter, "&o", &path)) {
        if (stringProperty(bus, path, kActiveIface, "Connection") == settingsPath)
            found = path;
    }
    g_variant_unref(active);
    return found;
}

void fillVpnProfile(GDBusConnection* bus, const std::string& path, GVariant* settings,
                    NmNet::VpnProfile& profile)
{
    profile.settingsPath = path;
    profile.name = stringSettingOf(settings, "connection", "id");
    profile.agentGuid = agentOfSettings(settings);
    profile.connectState = NmNet::kVpnStateDisconnected;
    if (profile.agentGuid == NmNet::kVpnAgentWireGuard) {
        profile.tunnelType = "WireGuard";
        profile.remote = stringSettingOf(settings, "wireguard", "endpoint");
        // Peer endpoint lives on the first peer; a simple single-peer form.
        GVariant* peers = settingOf(settings, "wireguard", "peers");
        if (peers && g_variant_is_of_type(peers, G_VARIANT_TYPE("aa{sv}"))) {
            GVariantIter iter;
            GVariant* peer = nullptr;
            g_variant_iter_init(&iter, peers);
            if (g_variant_iter_next(&iter, "@a{sv}", &peer)) {
                GVariant* endpoint = g_variant_lookup_value(peer, "endpoint", G_VARIANT_TYPE_STRING);
                if (endpoint) {
                    profile.remote = g_variant_get_string(endpoint, nullptr);
                    g_variant_unref(endpoint);
                }
                g_variant_unref(peer);
            }
        }
        if (peers)
            g_variant_unref(peers);
    } else {
        profile.tunnelType = "OpenVPN";
        profile.remote = vpnDataOf(settings, "remote");
        profile.userName = vpnDataOf(settings, "username");
    }
    const std::string active = activePathFor(bus, path);
    if (!active.empty()) {
        profile.connectState = connectStateOf(
            uintProperty(bus, active.c_str(), kActiveIface, "State", 0));
        // Prefer NM's State; older fakes may only expose Vpn=true.
        if (profile.connectState == NmNet::kVpnStateDisconnected
            && boolProperty(bus, active.c_str(), kActiveIface, "Vpn"))
            profile.connectState = NmNet::kVpnStateConnected;
    }
}

GVariant* openVpnSettings(const NmNet::VpnRequest& request)
{
    GVariantBuilder all;
    g_variant_builder_init(&all, G_VARIANT_TYPE("a{sa{sv}}"));

    GVariantBuilder connection;
    g_variant_builder_init(&connection, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&connection, "{sv}", "id", g_variant_new_string(request.name.c_str()));
    g_variant_builder_add(&connection, "{sv}", "type", g_variant_new_string("vpn"));
    g_variant_builder_add(&all, "{s@a{sv}}", "connection", g_variant_builder_end(&connection));

    GVariantBuilder vpn;
    g_variant_builder_init(&vpn, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&vpn, "{sv}", "service-type",
                          g_variant_new_string(NmNet::kNmOpenVpnService));
    GVariantBuilder data;
    g_variant_builder_init(&data, G_VARIANT_TYPE("a{ss}"));
    g_variant_builder_add(&data, "{ss}", "remote", request.remote.c_str());
    g_variant_builder_add(&data, "{ss}", "connection-type", "password");
    if (!request.userName.empty())
        g_variant_builder_add(&data, "{ss}", "username", request.userName.c_str());
    g_variant_builder_add(&vpn, "{sv}", "data", g_variant_builder_end(&data));
    if (!request.password.empty()) {
        GVariantBuilder secrets;
        g_variant_builder_init(&secrets, G_VARIANT_TYPE("a{ss}"));
        g_variant_builder_add(&secrets, "{ss}", "password", request.password.c_str());
        g_variant_builder_add(&vpn, "{sv}", "secrets", g_variant_builder_end(&secrets));
    }
    g_variant_builder_add(&all, "{s@a{sv}}", "vpn", g_variant_builder_end(&vpn));

    GVariantBuilder ipv4;
    g_variant_builder_init(&ipv4, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&ipv4, "{sv}", "method", g_variant_new_string("auto"));
    g_variant_builder_add(&all, "{s@a{sv}}", "ipv4", g_variant_builder_end(&ipv4));

    GVariantBuilder ipv6;
    g_variant_builder_init(&ipv6, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&ipv6, "{sv}", "method", g_variant_new_string("ignore"));
    g_variant_builder_add(&all, "{s@a{sv}}", "ipv6", g_variant_builder_end(&ipv6));
    return g_variant_builder_end(&all);
}

GVariant* wireGuardSettings(const NmNet::VpnRequest& request)
{
    GVariantBuilder all;
    g_variant_builder_init(&all, G_VARIANT_TYPE("a{sa{sv}}"));

    GVariantBuilder connection;
    g_variant_builder_init(&connection, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&connection, "{sv}", "id", g_variant_new_string(request.name.c_str()));
    g_variant_builder_add(&connection, "{sv}", "type", g_variant_new_string("wireguard"));
    g_variant_builder_add(&all, "{s@a{sv}}", "connection", g_variant_builder_end(&connection));

    GVariantBuilder wg;
    g_variant_builder_init(&wg, G_VARIANT_TYPE("a{sv}"));
    if (!request.privateKey.empty())
        g_variant_builder_add(&wg, "{sv}", "private-key",
                              g_variant_new_string(request.privateKey.c_str()));
    GVariantBuilder peers;
    g_variant_builder_init(&peers, G_VARIANT_TYPE("aa{sv}"));
    GVariantBuilder peer;
    g_variant_builder_init(&peer, G_VARIANT_TYPE("a{sv}"));
    if (!request.peerPublicKey.empty())
        g_variant_builder_add(&peer, "{sv}", "public-key",
                              g_variant_new_string(request.peerPublicKey.c_str()));
    if (!request.remote.empty())
        g_variant_builder_add(&peer, "{sv}", "endpoint",
                              g_variant_new_string(request.remote.c_str()));
    GVariantBuilder allowed;
    g_variant_builder_init(&allowed, G_VARIANT_TYPE("as"));
    g_variant_builder_add(&allowed, "s", "0.0.0.0/0");
    g_variant_builder_add(&peer, "{sv}", "allowed-ips", g_variant_builder_end(&allowed));
    g_variant_builder_add(&peers, "a{sv}", &peer);
    g_variant_builder_add(&wg, "{sv}", "peers", g_variant_builder_end(&peers));
    g_variant_builder_add(&all, "{s@a{sv}}", "wireguard", g_variant_builder_end(&wg));

    GVariantBuilder ipv4;
    g_variant_builder_init(&ipv4, G_VARIANT_TYPE("a{sv}"));
    if (!request.address.empty()) {
        g_variant_builder_add(&ipv4, "{sv}", "method", g_variant_new_string("manual"));
        GVariantBuilder addrs;
        g_variant_builder_init(&addrs, G_VARIANT_TYPE("aa{sv}"));
        GVariantBuilder addr;
        g_variant_builder_init(&addr, G_VARIANT_TYPE("a{sv}"));
        // "10.0.0.2/32" → address + prefix
        const size_t slash = request.address.find('/');
        const std::string host = slash == std::string::npos
            ? request.address : request.address.substr(0, slash);
        guint32 prefix = 32;
        if (slash != std::string::npos) {
            try {
                const int parsed = std::stoi(request.address.substr(slash + 1));
                if (parsed > 0 && parsed <= 32)
                    prefix = static_cast<guint32>(parsed);
            } catch (...) {
            }
        }
        g_variant_builder_add(&addr, "{sv}", "address", g_variant_new_string(host.c_str()));
        g_variant_builder_add(&addr, "{sv}", "prefix", g_variant_new_uint32(prefix));
        g_variant_builder_add(&addrs, "a{sv}", &addr);
        g_variant_builder_add(&ipv4, "{sv}", "address-data", g_variant_builder_end(&addrs));
    } else {
        g_variant_builder_add(&ipv4, "{sv}", "method", g_variant_new_string("auto"));
    }
    g_variant_builder_add(&all, "{s@a{sv}}", "ipv4", g_variant_builder_end(&ipv4));
    return g_variant_builder_end(&all);
}

GVariant* settingsFor(const NmNet::VpnRequest& request, std::string& error)
{
    if (request.name.empty()) {
        error = "profile name required";
        return nullptr;
    }
    if (!NmNet::knownVpnAgent(request.agentGuid)) {
        error = "unknown connection type";
        return nullptr;
    }
    if (request.remote.empty()) {
        error = "server required";
        return nullptr;
    }
    if (request.agentGuid == NmNet::kVpnAgentWireGuard)
        return wireGuardSettings(request);
    return openVpnSettings(request);
}

// Path of a VPN profile by display name, or empty.
std::string vpnPathByName(GDBusConnection* bus, const std::string& name, std::string& error)
{
    GVariant* reply = call(bus, kSettingsPath, kSettingsIface, "ListConnections", nullptr,
                           G_VARIANT_TYPE("(ao)"), error);
    if (!reply)
        return std::string();
    std::string found;
    GVariantIter* iter = nullptr;
    const char* path = nullptr;
    g_variant_get(reply, "(ao)", &iter);
    while (found.empty() && g_variant_iter_next(iter, "&o", &path)) {
        GVariant* settings = settingsOf(bus, path, error);
        if (!settings)
            continue;
        if (isVpnProfile(settings)
            && stringSettingOf(settings, "connection", "id") == name)
            found = path;
        g_variant_unref(settings);
    }
    g_variant_iter_free(iter);
    g_variant_unref(reply);
    if (found.empty())
        error = "no such profile";
    return found;
}

} // namespace

bool listVpnProfiles(GDBusConnection* bus, std::vector<NmNet::VpnProfile>& profiles,
                     std::string& error)
{
    if (!bus) {
        error = "no system bus";
        return false;
    }
    profiles.clear();
    GVariant* reply = call(bus, kSettingsPath, kSettingsIface, "ListConnections", nullptr,
                           G_VARIANT_TYPE("(ao)"), error);
    if (!reply)
        return false;
    GVariantIter* iter = nullptr;
    const char* path = nullptr;
    g_variant_get(reply, "(ao)", &iter);
    while (g_variant_iter_next(iter, "&o", &path)) {
        GVariant* settings = settingsOf(bus, path, error);
        if (!settings)
            continue;
        if (isVpnProfile(settings)) {
            NmNet::VpnProfile profile;
            fillVpnProfile(bus, path, settings, profile);
            profiles.push_back(profile);
        }
        g_variant_unref(settings);
    }
    g_variant_iter_free(iter);
    g_variant_unref(reply);
    error.clear();
    return true;
}

bool getVpnProfile(GDBusConnection* bus, const std::string& name, NmNet::VpnProfile& profile,
                   std::string& error)
{
    if (!bus) {
        error = "no system bus";
        return false;
    }
    const std::string path = vpnPathByName(bus, name, error);
    if (path.empty())
        return false;
    GVariant* settings = settingsOf(bus, path, error);
    if (!settings)
        return false;
    fillVpnProfile(bus, path, settings, profile);
    g_variant_unref(settings);
    return true;
}

bool addVpnProfile(GDBusConnection* bus, const NmNet::VpnRequest& request, std::string& error)
{
    if (!bus) {
        error = "no system bus";
        return false;
    }
    std::string ignored;
    if (!vpnPathByName(bus, request.name, ignored).empty()) {
        error = "profile with that name already exists";
        return false;
    }
    GVariant* settings = settingsFor(request, error);
    if (!settings)
        return false;
    GVariant* reply = call(bus, kSettingsPath, kSettingsIface, "AddConnection",
                           g_variant_new_tuple(&settings, 1), G_VARIANT_TYPE("(o)"), error);
    if (!reply)
        return false;
    g_variant_unref(reply);
    return true;
}

bool updateVpnProfile(GDBusConnection* bus, const NmNet::VpnRequest& request, std::string& error)
{
    if (!bus) {
        error = "no system bus";
        return false;
    }
    const std::string path = vpnPathByName(bus, request.name, error);
    if (path.empty())
        return false;
    GVariant* settings = settingsFor(request, error);
    if (!settings)
        return false;
    return update(bus, path, settings, error);
}

bool deleteVpnProfile(GDBusConnection* bus, const std::string& name, std::string& error)
{
    if (!bus) {
        error = "no system bus";
        return false;
    }
    const std::string path = vpnPathByName(bus, name, error);
    if (path.empty())
        return false;
    GVariant* reply = call(bus, path.c_str(), kConnectionIface, "Delete", nullptr, nullptr, error);
    if (!reply)
        return false;
    g_variant_unref(reply);
    return true;
}

bool connectVpn(GDBusConnection* bus, const std::string& name, std::string& error)
{
    if (!bus) {
        error = "no system bus";
        return false;
    }
    const std::string path = vpnPathByName(bus, name, error);
    if (path.empty())
        return false;
    // VPN activation does not need a specific device: NetworkManager picks the
    // default route's interface. "/" is what nmcli passes for that case.
    return activate(bus, path, "/", "/", error);
}

bool disconnectVpn(GDBusConnection* bus, std::string& error)
{
    if (!bus) {
        error = "no system bus";
        return false;
    }
    GVariant* active = property(bus, kNmPath, kNmIface, "ActiveConnections");
    if (!active) {
        error = "no active vpn";
        return false;
    }
    bool any = false;
    GVariantIter iter;
    const char* path = nullptr;
    g_variant_iter_init(&iter, active);
    while (g_variant_iter_next(&iter, "&o", &path)) {
        const bool isVpn = boolProperty(bus, path, kActiveIface, "Vpn");
        const std::string connection = stringProperty(bus, path, kActiveIface, "Connection");
        bool wireguard = false;
        if (!connection.empty()) {
            std::string ignored;
            GVariant* settings = settingsOf(bus, connection, ignored);
            if (settings) {
                wireguard = stringSettingOf(settings, "connection", "type") == "wireguard";
                g_variant_unref(settings);
            }
        }
        if (!isVpn && !wireguard)
            continue;
        GVariant* reply = call(bus, kNmPath, kNmIface, "DeactivateConnection",
                               g_variant_new("(o)", path), nullptr, error);
        if (!reply) {
            g_variant_unref(active);
            return false;
        }
        g_variant_unref(reply);
        any = true;
    }
    g_variant_unref(active);
    if (!any) {
        error = "no active vpn";
        return false;
    }
    return true;
}

} // namespace NmClient
