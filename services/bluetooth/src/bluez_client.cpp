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

#include "bluez_client.h"

namespace BtClient {

namespace {

const char kBluez[] = "org.bluez";
const char kAdapterIface[] = "org.bluez.Adapter1";
const char kDeviceIface[] = "org.bluez.Device1";
const char kBatteryIface[] = "org.bluez.Battery1";

// Set one writable BlueZ property. A failure keeps BlueZ's own message.
bool setProperty(GDBusConnection* bus, const char* path, const char* iface,
                 const char* name, GVariant* value, std::string& error)
{
    GError* gerror = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        bus, kBluez, path, "org.freedesktop.DBus.Properties", "Set",
        g_variant_new("(ssv)", iface, name, value), nullptr,
        G_DBUS_CALL_FLAGS_NONE, 8000, nullptr, &gerror);
    if (!reply) {
        error = gerror && gerror->message ? gerror->message : "no reply";
        g_clear_error(&gerror);
        return false;
    }
    g_variant_unref(reply);
    return true;
}

// A no-argument Device1/Adapter1 method (Connect, Disconnect, Pair, ...). The
// device/adapter path and the method are all that vary. error holds BlueZ's
// message, which is how the card learns a pairing was rejected or a connect
// timed out.
bool callOnObject(GDBusConnection* bus, const std::string& path, const char* iface,
                  const char* method, GVariant* args, std::string& error)
{
    if (path.empty()) {
        error = "no such object";
        return false;
    }
    GError* gerror = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        bus, kBluez, path.c_str(), iface, method, args, nullptr,
        G_DBUS_CALL_FLAGS_NONE, 30000, nullptr, &gerror);
    if (!reply) {
        error = gerror && gerror->message ? gerror->message : "no reply";
        g_clear_error(&gerror);
        return false;
    }
    g_variant_unref(reply);
    return true;
}

// Readers off an already-unpacked a{sv} properties dictionary -- the shape
// GetManagedObjects hands back per interface, so the whole tree is read in one
// round trip rather than a Properties.Get per field.
std::string dictString(GVariant* props, const char* key)
{
    GVariant* v = g_variant_lookup_value(props, key, nullptr);
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

bool dictBool(GVariant* props, const char* key)
{
    GVariant* v = g_variant_lookup_value(props, key, G_VARIANT_TYPE_BOOLEAN);
    if (!v)
        return false;
    const bool out = g_variant_get_boolean(v);
    g_variant_unref(v);
    return out;
}

guint32 dictUint(GVariant* props, const char* key)
{
    GVariant* v = g_variant_lookup_value(props, key, nullptr);
    if (!v)
        return 0;
    guint32 out = 0;
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_UINT32))
        out = g_variant_get_uint32(v);
    else if (g_variant_is_of_type(v, G_VARIANT_TYPE_UINT16))
        out = g_variant_get_uint16(v);
    else if (g_variant_is_of_type(v, G_VARIANT_TYPE_BYTE))
        out = g_variant_get_byte(v);
    g_variant_unref(v);
    return out;
}

int dictInt16(GVariant* props, const char* key, int fallback)
{
    GVariant* v = g_variant_lookup_value(props, key, G_VARIANT_TYPE_INT16);
    if (!v)
        return fallback;
    const int out = g_variant_get_int16(v);
    g_variant_unref(v);
    return out;
}

// Device1.UUIDs, an array of strings.
std::vector<std::string> dictUuids(GVariant* props)
{
    std::vector<std::string> out;
    GVariant* v = g_variant_lookup_value(props, "UUIDs", G_VARIANT_TYPE_STRING_ARRAY);
    if (!v)
        return out;
    GVariantIter iter;
    const char* uuid = nullptr;
    g_variant_iter_init(&iter, v);
    while (g_variant_iter_next(&iter, "&s", &uuid)) {
        if (uuid)
            out.emplace_back(uuid);
    }
    g_variant_unref(v);
    return out;
}

// GetManagedObjects on BlueZ's root: a{oa{sa{sv}}} -- object path to interface
// name to its properties. One call returns the adapter and every device.
GVariant* managedObjects(GDBusConnection* bus)
{
    if (!bus)
        return nullptr;
    GError* error = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        bus, kBluez, "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
        nullptr, G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
        G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &error);
    if (!reply) {
        g_clear_error(&error);
        return nullptr;
    }
    GVariant* objects = g_variant_get_child_value(reply, 0);
    g_variant_unref(reply);
    return objects;
}

} // namespace

std::string firstAdapterPath(GDBusConnection* bus)
{
    GVariant* objects = managedObjects(bus);
    if (!objects)
        return std::string();

    std::string found;
    GVariantIter iter;
    const char* path = nullptr;
    GVariant* ifaces = nullptr;
    g_variant_iter_init(&iter, objects);
    while (found.empty() && g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &path, &ifaces)) {
        if (g_variant_lookup_value(ifaces, kAdapterIface, nullptr))
            found = path ? path : "";
        g_variant_unref(ifaces);
    }
    g_variant_unref(objects);
    return found;
}

std::string devicePathForAddress(GDBusConnection* bus, const std::string& address)
{
    GVariant* objects = managedObjects(bus);
    if (!objects)
        return std::string();

    std::string found;
    GVariantIter iter;
    const char* path = nullptr;
    GVariant* ifaces = nullptr;
    g_variant_iter_init(&iter, objects);
    while (found.empty() && g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &path, &ifaces)) {
        GVariant* dev = g_variant_lookup_value(ifaces, kDeviceIface, nullptr);
        if (dev) {
            if (dictString(dev, "Address") == address)
                found = path ? path : "";
            g_variant_unref(dev);
        }
        g_variant_unref(ifaces);
    }
    g_variant_unref(objects);
    return found;
}

BtState::BluetoothState readState(GDBusConnection* bus)
{
    BtState::BluetoothState state;
    GVariant* objects = managedObjects(bus);
    if (!objects)
        return state;   // no BlueZ: no adapter, radio off

    GVariantIter iter;
    const char* path = nullptr;
    GVariant* ifaces = nullptr;
    g_variant_iter_init(&iter, objects);
    while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &path, &ifaces)) {
        GVariant* adapter = g_variant_lookup_value(ifaces, kAdapterIface, nullptr);
        if (adapter) {
            // The first adapter is the one webOS acts on; a second (a USB
            // dongle beside the built-in) is not a transport the UI knows.
            if (!state.adapter.present) {
                state.adapter.present = true;
                state.adapter.powered = dictBool(adapter, "Powered");
                state.adapter.discovering = dictBool(adapter, "Discovering");
                state.adapter.discoverable = dictBool(adapter, "Discoverable");
                state.adapter.pairable = dictBool(adapter, "Pairable");
                state.adapter.address = dictString(adapter, "Address");
                state.adapter.name = dictString(adapter, "Alias");
                if (state.adapter.name.empty())
                    state.adapter.name = dictString(adapter, "Name");
            }
            g_variant_unref(adapter);
        }

        GVariant* dev = g_variant_lookup_value(ifaces, kDeviceIface, nullptr);
        if (dev) {
            BtState::Device device;
            device.objectPath = path ? path : "";
            device.address = dictString(dev, "Address");
            device.name = dictString(dev, "Alias");
            if (device.name.empty())
                device.name = dictString(dev, "Name");
            device.cod = dictUint(dev, "Class");
            device.connected = dictBool(dev, "Connected");
            device.paired = dictBool(dev, "Paired");
            device.trusted = dictBool(dev, "Trusted");
            device.bonded = dictBool(dev, "Bonded");
            device.uuids = dictUuids(dev);
            device.addressType = dictString(dev, "AddressType");
            device.icon = dictString(dev, "Icon");
            device.rssi = dictInt16(dev, "RSSI", BtState::Device::kNoRssi);
            device.appearance = static_cast<int>(dictUint(dev, "Appearance"));
            if (!g_variant_lookup_value(dev, "Appearance", nullptr))
                device.appearance = BtState::Device::kNoAppearance;
            g_variant_unref(dev);

            // Battery1 is a separate interface on the same object; read it here
            // so the device carries its percentage without a second pass.
            GVariant* battery = g_variant_lookup_value(ifaces, kBatteryIface, nullptr);
            if (battery) {
                device.battery = static_cast<int>(dictUint(battery, "Percentage"));
                g_variant_unref(battery);
            }

            if (!device.address.empty())
                state.devices.push_back(device);
        }

        g_variant_unref(ifaces);
    }
    g_variant_unref(objects);
    return state;
}

bool setPowered(GDBusConnection* bus, bool on, bool visible, bool connectable,
                std::string& error)
{
    const std::string adapter = firstAdapterPath(bus);
    if (adapter.empty()) {
        error = "no bluetooth adapter";
        return false;
    }
    if (!setProperty(bus, adapter.c_str(), kAdapterIface, "Powered",
                     g_variant_new_boolean(on), error))
        return false;

    // On turn-on, follow the {"visible","connectable"} HP's radioon carries;
    // on turn-off there is nothing more to set. A failure to set these is not
    // fatal to the radio itself, so it does not undo the power change.
    if (on) {
        std::string ignore;
        setProperty(bus, adapter.c_str(), kAdapterIface, "Discoverable",
                    g_variant_new_boolean(visible), ignore);
        setProperty(bus, adapter.c_str(), kAdapterIface, "Pairable",
                    g_variant_new_boolean(connectable), ignore);
    }
    return true;
}

bool startDiscovery(GDBusConnection* bus, const std::string& transport, std::string& error)
{
    const std::string adapter = firstAdapterPath(bus);
    if (adapter.empty()) {
        error = "no bluetooth adapter";
        return false;
    }
    // SetDiscoveryFilter first when a transport was asked for. BlueZ keeps the
    // filter until it is cleared, so an empty transport means "leave it as is".
    if (!transport.empty()) {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&builder, "{sv}", "Transport",
                              g_variant_new_string(transport.c_str()));
        std::string ignore;
        callOnObject(bus, adapter, kAdapterIface, "SetDiscoveryFilter",
                     g_variant_new("(a{sv})", &builder), ignore);
    }
    return callOnObject(bus, adapter, kAdapterIface, "StartDiscovery", nullptr, error);
}

bool stopDiscovery(GDBusConnection* bus, std::string& error)
{
    const std::string adapter = firstAdapterPath(bus);
    if (adapter.empty()) {
        error = "no bluetooth adapter";
        return false;
    }
    return callOnObject(bus, adapter, kAdapterIface, "StopDiscovery", nullptr, error);
}

bool pairDevice(GDBusConnection* bus, const std::string& address, std::string& error)
{
    return callOnObject(bus, devicePathForAddress(bus, address), kDeviceIface,
                        "Pair", nullptr, error);
}

bool cancelPairing(GDBusConnection* bus, const std::string& address, std::string& error)
{
    return callOnObject(bus, devicePathForAddress(bus, address), kDeviceIface,
                        "CancelPairing", nullptr, error);
}

bool removeDevice(GDBusConnection* bus, const std::string& address, std::string& error)
{
    const std::string adapter = firstAdapterPath(bus);
    const std::string device = devicePathForAddress(bus, address);
    if (adapter.empty() || device.empty()) {
        error = "no such device";
        return false;
    }
    // RemoveDevice is on the adapter, taking the device path -- unlike the
    // Device1 methods, which are on the device itself.
    return callOnObject(bus, adapter, kAdapterIface, "RemoveDevice",
                        g_variant_new("(o)", device.c_str()), error);
}

bool setTrusted(GDBusConnection* bus, const std::string& address, bool trusted,
                std::string& error)
{
    const std::string device = devicePathForAddress(bus, address);
    if (device.empty()) {
        error = "no such device";
        return false;
    }
    return setProperty(bus, device.c_str(), kDeviceIface, "Trusted",
                       g_variant_new_boolean(trusted), error);
}

bool connectProfile(GDBusConnection* bus, const std::string& address,
                    const std::string& profile, std::string& error)
{
    const std::string device = devicePathForAddress(bus, address);
    if (device.empty()) {
        error = "no such device";
        return false;
    }
    const std::string uuid = BtState::uuidForProfile(profile);
    // "all", or a profile with no single UUID, connects the whole device;
    // otherwise connect just that profile's UUID.
    if (uuid.empty())
        return callOnObject(bus, device, kDeviceIface, "Connect", nullptr, error);
    return callOnObject(bus, device, kDeviceIface, "ConnectProfile",
                        g_variant_new("(s)", uuid.c_str()), error);
}

bool disconnectProfile(GDBusConnection* bus, const std::string& address,
                       const std::string& profile, std::string& error)
{
    const std::string device = devicePathForAddress(bus, address);
    if (device.empty()) {
        error = "no such device";
        return false;
    }
    const std::string uuid = BtState::uuidForProfile(profile);
    if (uuid.empty())
        return callOnObject(bus, device, kDeviceIface, "Disconnect", nullptr, error);
    return callOnObject(bus, device, kDeviceIface, "DisconnectProfile",
                        g_variant_new("(s)", uuid.c_str()), error);
}

} // namespace BtClient
