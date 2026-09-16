// What nm-connectionmanager reads from NetworkManager, and what it asks of it,
// against a fake NetworkManager on a private bus.
//
// tests/network-state.cpp checks what a NetworkState becomes on the webOS bus.
// This checks the other half: that the NetworkState is the one NetworkManager
// described. None of it can be seen from the mapping alone -- walking devices by
// type instead of trusting PrimaryConnection, an SSID arriving as bytes, the
// strength arriving as a byte, the address living on a separate object, and the
// two different calls that bring a cable up.
//
// The fake is served from its own thread, on its own connection, because the
// code under test makes blocking calls: served from the calling thread, every
// call would wait for a reply that thread could never send. It declares each
// method with the signature NetworkManager has, so a call built with the wrong
// argument types is refused here the way the real one would refuse it.
//
// The topology is the machine this was written for, measured:
//     enp0s31f6  type=1  state=20   Wired.Carrier=false   (cable out)
//     wlp0s20f3  type=2  state=100  strength=81  ip=192.168.1.66
//     tun0       type=16 state=100  (the VPN, and NM's PrimaryConnection)
//     br0        type=13 state=100  (a bridge webOS has no notion of)
#include "nm_client.h"

#include <gio/gio.h>

#include <condition_variable>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("  %-66s %s\n", what, ok ? "OK" : "<-- FAIL");
    if (!ok)
        ++g_failures;
}

static bool has(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

static const char kIntrospection[] = R"(
<node>
  <interface name="org.freedesktop.NetworkManager">
    <property name="Connectivity" type="u" access="read"/>
    <property name="Devices" type="ao" access="read"/>
    <property name="ActiveConnections" type="ao" access="read"/>
    <method name="ActivateConnection">
      <arg name="connection" type="o" direction="in"/>
      <arg name="device" type="o" direction="in"/>
      <arg name="specific_object" type="o" direction="in"/>
      <arg name="active_connection" type="o" direction="out"/>
    </method>
    <method name="AddAndActivateConnection">
      <arg name="connection" type="a{sa{sv}}" direction="in"/>
      <arg name="device" type="o" direction="in"/>
      <arg name="specific_object" type="o" direction="in"/>
      <arg name="path" type="o" direction="out"/>
      <arg name="active_connection" type="o" direction="out"/>
    </method>
  </interface>
  <interface name="org.freedesktop.NetworkManager.Device">
    <property name="DeviceType" type="u" access="read"/>
    <property name="State" type="u" access="read"/>
    <property name="Interface" type="s" access="read"/>
    <property name="Ip4Config" type="o" access="read"/>
    <property name="AvailableConnections" type="ao" access="read"/>
    <method name="Disconnect"/>
  </interface>
  <interface name="org.freedesktop.NetworkManager.Device.Wired">
    <property name="Carrier" type="b" access="read"/>
  </interface>
  <interface name="org.freedesktop.NetworkManager.Device.Wireless">
    <property name="ActiveAccessPoint" type="o" access="read"/>
  </interface>
  <interface name="org.freedesktop.NetworkManager.AccessPoint">
    <property name="Ssid" type="ay" access="read"/>
    <property name="Strength" type="y" access="read"/>
  </interface>
  <interface name="org.freedesktop.NetworkManager.IP4Config">
    <property name="AddressData" type="aa{sv}" access="read"/>
  </interface>
  <interface name="org.freedesktop.NetworkManager.Connection.Active">
    <property name="Vpn" type="b" access="read"/>
  </interface>
</node>
)";

#define NM "/org/freedesktop/NetworkManager"
#define ETH NM "/Devices/1"
#define WIFI NM "/Devices/2"
#define TUN NM "/Devices/3"
#define BRIDGE NM "/Devices/4"
#define WIFI2 NM "/Devices/5"
#define AP NM "/AccessPoint/1"
#define IP_WIFI NM "/IP4Config/1"
#define IP_ETH NM "/IP4Config/2"
#define AC_WIFI NM "/ActiveConnection/1"
#define AC_VPN NM "/ActiveConnection/2"
#define SAVED NM "/Settings/7"

#define I_NM "org.freedesktop.NetworkManager"
#define I_DEV I_NM ".Device"
#define I_WIRED I_NM ".Device.Wired"
#define I_WIRELESS I_NM ".Device.Wireless"
#define I_AP I_NM ".AccessPoint"
#define I_IP4 I_NM ".IP4Config"
#define I_ACTIVE I_NM ".Connection.Active"

// The fake NetworkManager. Properties are a table the test rewrites between
// cases; a property missing from it is answered with an error, which is what a
// device that vanished between being listed and being read looks like.
class FakeNm {
public:
    explicit FakeNm(const std::string& address)
        : m_address(address)
    {
        m_thread = std::thread([this] { serve(); });
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_ready; });
    }

    ~FakeNm()
    {
        g_main_loop_quit(m_loop);
        m_thread.join();
        for (auto& entry : m_props)
            g_variant_unref(entry.second);
    }

    void set(const char* path, const char* iface, const char* name, GVariant* value)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const std::string key = keyOf(path, iface, name);
        auto it = m_props.find(key);
        if (it != m_props.end())
            g_variant_unref(it->second);
        m_props[key] = g_variant_ref_sink(value);
    }

    void unset(const char* path, const char* iface, const char* name)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_props.find(keyOf(path, iface, name));
        if (it != m_props.end()) {
            g_variant_unref(it->second);
            m_props.erase(it);
        }
    }

    // The next method call fails with this message, as NetworkManager's refusals do.
    void failNextCall(const std::string& message)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_failWith = message;
    }

    // Each call as "path method args", in the order they arrived; cleared on read.
    // The arguments are printed without their types: GDBus has already refused
    // any call whose signature differs from the one declared above.
    std::vector<std::string> takeCalls()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::string> out;
        out.swap(m_calls);
        return out;
    }

private:
    static std::string keyOf(const char* path, const char* iface, const char* name)
    {
        return std::string(path) + "|" + iface + "|" + name;
    }

    static GVariant* getProperty(GDBusConnection*, const gchar*, const gchar* path,
                                 const gchar* iface, const gchar* name,
                                 GError** error, gpointer self)
    {
        FakeNm* fake = static_cast<FakeNm*>(self);
        std::lock_guard<std::mutex> lock(fake->m_mutex);
        auto it = fake->m_props.find(keyOf(path, iface, name));
        if (it == fake->m_props.end()) {
            g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_OBJECT,
                        "no %s.%s on %s", iface, name, path);
            return nullptr;
        }
        return g_variant_ref(it->second);
    }

    static void methodCall(GDBusConnection*, const gchar*, const gchar* path,
                           const gchar*, const gchar* method, GVariant* params,
                           GDBusMethodInvocation* invocation, gpointer self)
    {
        FakeNm* fake = static_cast<FakeNm*>(self);
        std::string failWith;
        {
            std::lock_guard<std::mutex> lock(fake->m_mutex);
            gchar* printed = g_variant_print(params, FALSE);
            fake->m_calls.push_back(std::string(path) + " " + method + " " + printed);
            g_free(printed);
            failWith.swap(fake->m_failWith);
        }
        if (!failWith.empty()) {
            g_dbus_method_invocation_return_dbus_error(
                invocation, "org.freedesktop.NetworkManager.Device.NotActive", failWith.c_str());
            return;
        }
        const std::string name = method;
        if (name == "ActivateConnection")
            g_dbus_method_invocation_return_value(invocation,
                                                  g_variant_new("(o)", NM "/ActiveConnection/9"));
        else if (name == "AddAndActivateConnection")
            g_dbus_method_invocation_return_value(
                invocation, g_variant_new("(oo)", NM "/Settings/9", NM "/ActiveConnection/9"));
        else
            g_dbus_method_invocation_return_value(invocation, nullptr);
    }

    void serve()
    {
        GMainContext* context = g_main_context_new();
        g_main_context_push_thread_default(context);
        m_loop = g_main_loop_new(context, FALSE);

        GError* error = nullptr;
        GDBusConnection* bus = g_dbus_connection_new_for_address_sync(
            m_address.c_str(),
            static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT
                                              | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
            nullptr, nullptr, &error);
        g_assert_no_error(error);

        GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(kIntrospection, &error);
        g_assert_no_error(error);
        const GDBusInterfaceVTable vtable = { methodCall, getProperty, nullptr, { nullptr } };
        const std::vector<std::pair<const char*, std::vector<const char*>>> objects = {
            { NM, { I_NM } },
            { ETH, { I_DEV, I_WIRED, I_WIRELESS } },
            { WIFI, { I_DEV, I_WIRED, I_WIRELESS } },
            { TUN, { I_DEV } },
            { BRIDGE, { I_DEV } },
            { WIFI2, { I_DEV, I_WIRED, I_WIRELESS } },
            { AP, { I_AP } },
            { IP_WIFI, { I_IP4 } },
            { IP_ETH, { I_IP4 } },
            { AC_WIFI, { I_ACTIVE } },
            { AC_VPN, { I_ACTIVE } },
        };
        for (const auto& object : objects) {
            for (const char* iface : object.second) {
                g_dbus_connection_register_object(
                    bus, object.first, g_dbus_node_info_lookup_interface(node, iface),
                    &vtable, this, nullptr, &error);
                g_assert_no_error(error);
            }
        }

        GVariant* reply = g_dbus_connection_call_sync(
            bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
            "RequestName", g_variant_new("(su)", I_NM, 4 /* DO_NOT_QUEUE */),
            G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
        g_assert_no_error(error);
        g_variant_unref(reply);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_ready = true;
        }
        m_cv.notify_one();

        g_main_loop_run(m_loop);

        g_dbus_node_info_unref(node);
        g_object_unref(bus);
        g_main_loop_unref(m_loop);
        g_main_context_pop_thread_default(context);
        g_main_context_unref(context);
    }

    std::string m_address;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_ready = false;
    GMainLoop* m_loop = nullptr;
    std::map<std::string, GVariant*> m_props;
    std::vector<std::string> m_calls;
    std::string m_failWith;
};

static GVariant* paths(std::initializer_list<const char*> list)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("ao"));
    for (const char* path : list)
        g_variant_builder_add(&builder, "o", path);
    return g_variant_builder_end(&builder);
}

static GVariant* addresses(std::initializer_list<const char*> list)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("aa{sv}"));
    for (const char* address : list) {
        GVariantBuilder entry;
        g_variant_builder_init(&entry, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&entry, "{sv}", "address", g_variant_new_string(address));
        g_variant_builder_add(&entry, "{sv}", "prefix", g_variant_new_uint32(24));
        g_variant_builder_add(&builder, "a{sv}", &entry);
    }
    return g_variant_builder_end(&builder);
}

static GVariant* bytes(const std::string& s)
{
    return g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, s.data(), s.size(), 1);
}

static void device(FakeNm& nm, const char* path, guint32 type, guint32 state,
                   const char* iface, const char* ip4 = "/")
{
    nm.set(path, I_DEV, "DeviceType", g_variant_new_uint32(type));
    nm.set(path, I_DEV, "State", g_variant_new_uint32(state));
    nm.set(path, I_DEV, "Interface", g_variant_new_string(iface));
    nm.set(path, I_DEV, "Ip4Config", g_variant_new_object_path(ip4));
}

// The laptop as measured: wifi up behind a VPN, the cable out, and two devices
// that are up but are not transports webOS knows.
static void measuredLaptop(FakeNm& nm)
{
    nm.set(NM, I_NM, "Connectivity", g_variant_new_uint32(4));
    nm.set(NM, I_NM, "Devices", paths({ ETH, WIFI, TUN, BRIDGE }));
    nm.set(NM, I_NM, "ActiveConnections", paths({ AC_WIFI, AC_VPN }));
    nm.set(AC_WIFI, I_ACTIVE, "Vpn", g_variant_new_boolean(FALSE));
    nm.set(AC_VPN, I_ACTIVE, "Vpn", g_variant_new_boolean(TRUE));

    device(nm, ETH, 1, 20, "enp0s31f6");
    nm.set(ETH, I_WIRED, "Carrier", g_variant_new_boolean(FALSE));
    nm.set(ETH, I_DEV, "AvailableConnections", paths({}));

    device(nm, WIFI, 2, 100, "wlp0s20f3", IP_WIFI);
    nm.set(WIFI, I_WIRELESS, "ActiveAccessPoint", g_variant_new_object_path(AP));
    // Not valid UTF-8 and not NUL-free: an SSID is whatever the access point sends.
    nm.set(AP, I_AP, "Ssid", bytes(std::string("Gach\0WL\xff", 8)));
    nm.set(AP, I_AP, "Strength", g_variant_new_byte(81));
    nm.set(IP_WIFI, I_IP4, "AddressData", addresses({ "192.168.1.66", "192.168.1.67" }));

    device(nm, TUN, 16, 100, "tun0");
    device(nm, BRIDGE, 13, 100, "br0");

    device(nm, WIFI2, 2, 30, "wlx00c0ca");
    nm.set(WIFI2, I_WIRELESS, "ActiveAccessPoint", g_variant_new_object_path("/"));
}

static void run(const std::string& address)
{
    GError* error = nullptr;
    GDBusConnection* bus = g_dbus_connection_new_for_address_sync(
        address.c_str(),
        static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT
                                          | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, nullptr, &error);
    g_assert_no_error(error);

    std::printf("no NetworkManager at all\n");
    {
        const NmNet::NetworkState none = NmClient::readState(bus);
        check(none.connectivity == NmNet::kConnectivityUnknown, "connectivity reads unknown");
        check(!none.wifi.present && !none.wired.present, "and no devices");
        const NmNet::NetworkState noBus = NmClient::readState(nullptr);
        check(!noBus.wifi.present && !noBus.wired.present, "no bus reads the same");
        std::string why;
        check(!NmClient::setWired(bus, true, why) && why == "no wired device",
              "connecting the cable says there is no socket");
    }

    FakeNm nm(address);
    measuredLaptop(nm);

    std::printf("the laptop as measured\n");
    {
        const NmNet::NetworkState s = NmClient::readState(bus);
        check(s.connectivity == 4, "connectivity is NM's");
        check(s.wifi.activated() && s.wifi.interfaceName == "wlp0s20f3", "wifi is the device of type 2");
        check(s.wifi.ssid == std::string("Gach\0WL\xff", 8), "the SSID comes through byte for byte");
        check(s.wifi.strength == 81, "the strength arrives as a byte and is read");
        check(s.wifi.ipAddress == "192.168.1.66", "the first address of its IP4Config");
        check(s.wired.present && s.wired.interfaceName == "enp0s31f6", "wired is the device of type 1");
        check(!s.wired.activated() && s.wired.state == 20, "the cable reads unavailable, not the up bridge");
        check(!s.wired.carrier, "and without carrier");
        check(s.wired.ipAddress.empty(), "and without an address");
        check(s.vpnActive, "a VPN among the active connections is seen");
    }

    std::printf("the cable in and up, no VPN\n");
    {
        device(nm, ETH, 1, 100, "enp0s31f6", IP_ETH);
        nm.set(ETH, I_WIRED, "Carrier", g_variant_new_boolean(TRUE));
        nm.set(IP_ETH, I_IP4, "AddressData", addresses({ "192.168.1.20" }));
        nm.set(NM, I_NM, "ActiveConnections", paths({ AC_WIFI }));
        const NmNet::NetworkState s = NmClient::readState(bus);
        check(s.wired.activated() && s.wired.carrier, "wired is up, with carrier");
        check(s.wired.ipAddress == "192.168.1.20", "with its own address");
        check(!s.vpnActive, "and no VPN");
        measuredLaptop(nm);
    }

    std::printf("a vanished property\n");
    {
        nm.unset(AP, I_AP, "Strength");
        const NmNet::NetworkState s = NmClient::readState(bus);
        check(s.wifi.activated() && s.wifi.strength == 0, "a missing strength reads 0, the rest survives");
        measuredLaptop(nm);
    }

    std::printf("two wifi adapters\n");
    {
        nm.set(NM, I_NM, "Devices", paths({ WIFI2, WIFI }));
        NmNet::NetworkState s = NmClient::readState(bus);
        check(s.wifi.interfaceName == "wlp0s20f3", "the activated one wins over the first seen");
        nm.set(WIFI, I_DEV, "State", g_variant_new_uint32(30));
        s = NmClient::readState(bus);
        check(s.wifi.interfaceName == "wlx00c0ca", "with neither up, the first seen is kept");
        check(s.wifi.ssid.empty() && s.wifi.strength == 0, "and no access point reads no name, no signal");
        nm.set(NM, I_NM, "Devices", paths({ WIFI, WIFI2 }));
        nm.set(WIFI2, I_DEV, "State", g_variant_new_uint32(100));
        s = NmClient::readState(bus);
        check(s.wifi.interfaceName == "wlx00c0ca", "an activated one later in the list also wins");
        measuredLaptop(nm);
    }

    std::printf("the cable, connected and disconnected\n");
    {
        nm.takeCalls();
        std::string why;
        check(NmClient::setWired(bus, true, why), "connecting with no saved profile succeeds");
        std::vector<std::string> calls = nm.takeCalls();
        check(calls.size() == 1
                  && calls[0] == NM " AddAndActivateConnection ({}, '" ETH "', '/')",
              "NM is asked to build one, for the socket");

        nm.set(ETH, I_DEV, "AvailableConnections", paths({ SAVED, NM "/Settings/8" }));
        check(NmClient::setWired(bus, true, why), "connecting with saved profiles succeeds");
        calls = nm.takeCalls();
        check(calls.size() == 1
                  && calls[0] == NM " ActivateConnection ('" SAVED "', '" ETH "', '/')",
              "the first saved one is activated on the socket");

        check(NmClient::setWired(bus, false, why), "disconnecting succeeds");
        calls = nm.takeCalls();
        check(calls.size() == 1 && calls[0] == ETH " Disconnect ()", "Disconnect goes to the socket itself");

        nm.set(NM, I_NM, "Devices", paths({ WIFI, BRIDGE, ETH }));
        NmClient::setWired(bus, false, why);
        calls = nm.takeCalls();
        check(calls.size() == 1 && has(calls[0], ETH " "), "the socket is found wherever it is listed");

        nm.failNextCall("because device has no carrier");
        why.clear();
        check(!NmClient::setWired(bus, true, why), "a refusal is a failure");
        check(has(why, "because device has no carrier"), "carrying NM's own words");
        nm.takeCalls();

        nm.set(NM, I_NM, "Devices", paths({ WIFI, TUN, BRIDGE }));
        why.clear();
        check(!NmClient::setWired(bus, false, why) && why == "no wired device",
              "a machine without a socket says so");
        check(nm.takeCalls().empty(), "and asks NM nothing");
    }

    g_object_unref(bus);
}

int main()
{
    // A private bus with its own dbus-daemon, torn down at the end. The fake
    // NetworkManager lives inside run() so it is gone before the bus is.
    GTestDBus* testBus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(testBus);
    run(g_test_dbus_get_bus_address(testBus));
    g_test_dbus_down(testBus);
    g_object_unref(testBus);

    std::printf("\n%s\n", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
