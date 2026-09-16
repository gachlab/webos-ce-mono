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

void readDevice(GDBusConnection* bus, const char* path, guint32 type, NmNet::Device& out)
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

    if (!out.present || (device.activated() && !out.activated()))
        out = device;
}

} // namespace

NmNet::NetworkState readState(GDBusConnection* bus)
{
    NmNet::NetworkState state;
    if (!bus)
        return state;   // no bus: everything disconnected, which is the truth

    state.connectivity = static_cast<int>(
        uintProperty(bus, kNmPath, kNmIface, "Connectivity", NmNet::kConnectivityUnknown));

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
            if (type == NmNet::kDeviceWifi)
                readDevice(bus, path, type, state.wifi);
            else if (type == NmNet::kDeviceEthernet)
                readDevice(bus, path, type, state.wired);
        }
        g_variant_unref(devices);
    }

    state.vpnActive = anyVpnActive(bus);
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

} // namespace NmClient
