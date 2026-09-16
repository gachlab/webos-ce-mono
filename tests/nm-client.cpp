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
#include "sleep_watch.h"

#include <gio/gunixfdlist.h>
#include <fcntl.h>
#include <unistd.h>

#include <gio/gio.h>

#include <cerrno>
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

static const char kIntrospection[] = R"XML(
<node>
  <interface name="org.freedesktop.NetworkManager">
    <property name="Connectivity" type="u" access="read"/>
    <property name="Devices" type="ao" access="read"/>
    <property name="ActiveConnections" type="ao" access="read"/>
    <property name="WirelessEnabled" type="b" access="readwrite"/>
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
    <property name="StateReason" type="(uu)" access="read"/>
    <property name="ActiveConnection" type="o" access="read"/>
    <property name="HwAddress" type="s" access="read"/>
    <method name="Disconnect"/>
  </interface>
  <interface name="org.freedesktop.NetworkManager.Device.Wired">
    <property name="Carrier" type="b" access="read"/>
  </interface>
  <interface name="org.freedesktop.NetworkManager.Device.Wireless">
    <property name="ActiveAccessPoint" type="o" access="read"/>
    <property name="AccessPoints" type="ao" access="read"/>
    <method name="RequestScan">
      <arg name="options" type="a{sv}" direction="in"/>
    </method>
  </interface>
  <interface name="org.freedesktop.NetworkManager.AccessPoint">
    <property name="Ssid" type="ay" access="read"/>
    <property name="Strength" type="y" access="read"/>
    <property name="Flags" type="u" access="read"/>
    <property name="WpaFlags" type="u" access="read"/>
    <property name="RsnFlags" type="u" access="read"/>
    <property name="HwAddress" type="s" access="read"/>
    <property name="Frequency" type="u" access="read"/>
  </interface>
  <interface name="org.freedesktop.NetworkManager.IP4Config">
    <property name="AddressData" type="aa{sv}" access="read"/>
    <property name="Gateway" type="s" access="read"/>
    <property name="NameserverData" type="aa{sv}" access="read"/>
  </interface>
  <interface name="org.freedesktop.NetworkManager.Connection.Active">
    <property name="Vpn" type="b" access="read"/>
    <property name="Connection" type="o" access="read"/>
  </interface>
  <interface name="org.freedesktop.login1.Manager">
    <method name="Inhibit">
      <arg name="what" type="s" direction="in"/>
      <arg name="who" type="s" direction="in"/>
      <arg name="why" type="s" direction="in"/>
      <arg name="mode" type="s" direction="in"/>
      <arg name="fd" type="h" direction="out"/>
    </method>
    <signal name="PrepareForSleep">
      <arg name="start" type="b"/>
    </signal>
  </interface>
  <interface name="org.freedesktop.NetworkManager.Settings">
    <method name="ListConnections">
      <arg name="connections" type="ao" direction="out"/>
    </method>
  </interface>
  <interface name="org.freedesktop.NetworkManager.Settings.Connection">
    <method name="GetSettings">
      <arg name="settings" type="a{sa{sv}}" direction="out"/>
    </method>
    <method name="Update">
      <arg name="properties" type="a{sa{sv}}" direction="in"/>
    </method>
    <method name="Delete"/>
  </interface>
</node>
)XML";

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
#define SAVED_WIFI NM "/Settings/3"
#define SAVED_VPN NM "/Settings/5"
#define AP2 NM "/AccessPoint/2"
#define AP3 NM "/AccessPoint/3"
#define AP4 NM "/AccessPoint/4"
#define SETTINGS NM "/Settings"

#define I_NM "org.freedesktop.NetworkManager"
#define I_DEV I_NM ".Device"
#define I_WIRED I_NM ".Device.Wired"
#define I_WIRELESS I_NM ".Device.Wireless"
#define I_AP I_NM ".AccessPoint"
#define I_IP4 I_NM ".IP4Config"
#define I_ACTIVE I_NM ".Connection.Active"
#define I_SETTINGS I_NM ".Settings"
#define I_CONN I_NM ".Settings.Connection"

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
    // Whether logind would still be waiting: every lock handed out whose holder
    // has not closed it.
    int heldInhibitors()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        int held = 0;
        for (int fd : m_inhibitReadEnds) {
            char byte;
            const int flags = fcntl(fd, F_GETFL);
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
            if (read(fd, &byte, 1) < 0 && errno == EAGAIN)
                ++held;    // no EOF: the other end is still open
        }
        return held;
    }

    void emitPrepareForSleep(bool sleeping)
    {
        GDBusConnection* bus = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            bus = m_bus;
        }
        g_dbus_connection_emit_signal(bus, nullptr, "/org/freedesktop/login1",
                                      "org.freedesktop.login1.Manager", "PrepareForSleep",
                                      g_variant_new("(b)", sleeping), nullptr);
        g_dbus_connection_flush_sync(bus, nullptr, nullptr);
    }

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

    static gboolean setProperty(GDBusConnection*, const gchar*, const gchar* path,
                                const gchar* iface, const gchar* name, GVariant* value,
                                GError**, gpointer self)
    {
        FakeNm* fake = static_cast<FakeNm*>(self);
        std::lock_guard<std::mutex> lock(fake->m_mutex);
        gchar* printed = g_variant_print(value, FALSE);
        fake->m_calls.push_back(std::string(path) + " Set " + name + " " + printed);
        g_free(printed);
        const std::string key = keyOf(path, iface, name);
        auto it = fake->m_props.find(key);
        if (it != fake->m_props.end())
            g_variant_unref(it->second);
        fake->m_props[key] = g_variant_ref(value);
        return TRUE;
    }

    static void methodCall(GDBusConnection*, const gchar*, const gchar* path,
                           const gchar*, const gchar* method, GVariant* params,
                           GDBusMethodInvocation* invocation, gpointer self)
    {
        FakeNm* fake = static_cast<FakeNm*>(self);
        const std::string name = method;

        // Reads, answered from the table: not recorded, and not what a
        // programmed failure is for.
        if (name == "ListConnections" || name == "GetSettings") {
            GVariant* value = nullptr;
            {
                std::lock_guard<std::mutex> lock(fake->m_mutex);
                auto it = fake->m_props.find(
                    keyOf(path, name == "ListConnections" ? I_SETTINGS : I_CONN, "@reply"));
                if (it != fake->m_props.end())
                    value = g_variant_ref(it->second);
            }
            if (!value) {
                g_dbus_method_invocation_return_dbus_error(
                    invocation, "org.freedesktop.NetworkManager.Settings.Connection.Error",
                    "no such connection");
                return;
            }
            g_dbus_method_invocation_return_value(invocation, g_variant_new_tuple(&value, 1));
            g_variant_unref(value);
            return;
        }

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
        if (name == "Inhibit") {
            int ends[2];
            if (pipe2(ends, O_CLOEXEC) != 0) {
                g_dbus_method_invocation_return_dbus_error(invocation, "org.test.Error", "pipe");
                return;
            }
            GUnixFDList* fds = g_unix_fd_list_new();
            g_unix_fd_list_append(fds, ends[1], nullptr);
            close(ends[1]);
            {
                std::lock_guard<std::mutex> lock(fake->m_mutex);
                fake->m_inhibitReadEnds.push_back(ends[0]);
            }
            g_dbus_method_invocation_return_value_with_unix_fd_list(invocation, g_variant_new("(h)", 0), fds);
            g_object_unref(fds);
            return;
        }
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
        const GDBusInterfaceVTable vtable = { methodCall, getProperty, setProperty, { nullptr } };
        const std::vector<std::pair<const char*, std::vector<const char*>>> objects = {
            { NM, { I_NM } },
            { ETH, { I_DEV, I_WIRED, I_WIRELESS } },
            { WIFI, { I_DEV, I_WIRED, I_WIRELESS } },
            { TUN, { I_DEV } },
            { BRIDGE, { I_DEV } },
            { WIFI2, { I_DEV, I_WIRED, I_WIRELESS } },
            { AP, { I_AP } },
            { AP2, { I_AP } },
            { AP3, { I_AP } },
            { AP4, { I_AP } },
            { SETTINGS, { I_SETTINGS } },
            { "/org/freedesktop/login1", { "org.freedesktop.login1.Manager" } },
            { SAVED, { I_CONN } },
            { SAVED_WIFI, { I_CONN } },
            { SAVED_VPN, { I_CONN } },
            { NM "/Settings/9", { I_CONN } },
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

        for (const char* name : { I_NM, "org.freedesktop.login1" }) {
            GVariant* reply = g_dbus_connection_call_sync(
                bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                "RequestName", g_variant_new("(su)", name, 4 /* DO_NOT_QUEUE */),
                G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
            g_assert_no_error(error);
            g_variant_unref(reply);
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_ready = true;
            m_bus = bus;
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
    std::vector<int> m_inhibitReadEnds;
    GDBusConnection* m_bus = nullptr;
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

// A connection's settings, as GetSettings answers: {"connection": {...}, ...}.
static GVariant* settings(const char* type, const char* id, const std::string& ssid = std::string(),
                          const char* keyMgmt = nullptr, const char* ipv4Method = nullptr)
{
    GVariantBuilder all;
    g_variant_builder_init(&all, G_VARIANT_TYPE("a{sa{sv}}"));
    GVariantBuilder connection;
    g_variant_builder_init(&connection, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&connection, "{sv}", "type", g_variant_new_string(type));
    g_variant_builder_add(&connection, "{sv}", "id", g_variant_new_string(id));
    g_variant_builder_add(&all, "{s@a{sv}}", "connection", g_variant_builder_end(&connection));
    if (!ssid.empty()) {
        GVariantBuilder wireless;
        g_variant_builder_init(&wireless, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&wireless, "{sv}", "ssid", bytes(ssid));
        g_variant_builder_add(&all, "{s@a{sv}}", "802-11-wireless", g_variant_builder_end(&wireless));
    }
    if (keyMgmt) {
        GVariantBuilder sec;
        g_variant_builder_init(&sec, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&sec, "{sv}", "key-mgmt", g_variant_new_string(keyMgmt));
        g_variant_builder_add(&all, "{s@a{sv}}", "802-11-wireless-security", g_variant_builder_end(&sec));
    }
    if (ipv4Method) {
        GVariantBuilder ip;
        g_variant_builder_init(&ip, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&ip, "{sv}", "method", g_variant_new_string(ipv4Method));
        g_variant_builder_add(&all, "{s@a{sv}}", "ipv4", g_variant_builder_end(&ip));
    }
    return g_variant_builder_end(&all);
}

static void accessPoint(FakeNm& nm, const char* path, const std::string& ssid, guint8 strength,
                        guint32 flags, guint32 wpa, guint32 rsn)
{
    nm.set(path, I_AP, "Ssid", bytes(ssid));
    nm.set(path, I_AP, "Strength", g_variant_new_byte(strength));
    nm.set(path, I_AP, "Flags", g_variant_new_uint32(flags));
    nm.set(path, I_AP, "WpaFlags", g_variant_new_uint32(wpa));
    nm.set(path, I_AP, "RsnFlags", g_variant_new_uint32(rsn));
}

// Only the calls that change something; the reads are not recorded.
static std::vector<std::string> writes(FakeNm& nm)
{
    std::vector<std::string> out;
    for (const std::string& c : nm.takeCalls())
        if (c.find(" RequestScan ") == std::string::npos)
            out.push_back(c);
    return out;
}

static bool anyHas(const std::vector<std::string>& calls, const std::string& needle)
{
    for (const std::string& c : calls)
        if (has(c, needle))
            return true;
    return false;
}

// A wifi setup: the laptop's radio with three networks in range and a saved
// profile for one of them, beside a saved VPN.
static void wifiScene(FakeNm& nm)
{
    nm.set(WIFI, I_WIRELESS, "AccessPoints", paths({ AP, AP2, AP3, AP4 }));
    nm.set(WIFI, I_DEV, "HwAddress", g_variant_new_string("7C:21:4A:00:11:22"));
    nm.set(WIFI, I_DEV, "StateReason", g_variant_new("(uu)", 100, 0));
    nm.set(WIFI, I_DEV, "ActiveConnection", g_variant_new_object_path(AC_WIFI));
    nm.set(AC_WIFI, I_ACTIVE, "Connection", g_variant_new_object_path(SAVED_WIFI));
    // The joined network, open, weak.
    accessPoint(nm, AP, "Home", 81, 0, 0, 0);
    // A WPA2 network seen through two access points.
    accessPoint(nm, AP2, "Office", 40, 1, 0, 0x100);
    accessPoint(nm, AP3, "Office", 90, 1, 0, 0x100);
    // WPA3 only.
    accessPoint(nm, AP4, "Modern", 60, 1, 0, 0x400);
    nm.set(SETTINGS, I_SETTINGS, "@reply", paths({ SAVED_VPN, SAVED_WIFI, SAVED }));
    nm.set(SAVED_WIFI, I_CONN, "@reply", settings("802-11-wireless", "Home", "Home"));
    nm.set(SAVED_VPN, I_CONN, "@reply", settings("vpn", "Work VPN"));
    nm.set(SAVED, I_CONN, "@reply", settings("802-3-ethernet", "Wired"));
    nm.set(IP_WIFI, I_IP4, "Gateway", g_variant_new_string("192.168.1.1"));
    GVariantBuilder dns;
    g_variant_builder_init(&dns, G_VARIANT_TYPE("aa{sv}"));
    for (const char* a : { "1.1.1.1", "9.9.9.9", "8.8.8.8" }) {
        GVariantBuilder e;
        g_variant_builder_init(&e, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&e, "{sv}", "address", g_variant_new_string(a));
        g_variant_builder_add(&dns, "a{sv}", &e);
    }
    nm.set(IP_WIFI, I_IP4, "NameserverData", g_variant_builder_end(&dns));
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


    std::printf("the radio, and why the device is in its state\n");
    {
        wifiScene(nm);
        NmNet::NetworkState s = NmClient::readState(bus);
        check(s.wifiEnabled, "WirelessEnabled missing reads as on");
        nm.set(NM, I_NM, "WirelessEnabled", g_variant_new_boolean(FALSE));
        s = NmClient::readState(bus);
        check(!s.wifiEnabled, "WirelessEnabled false reads as off");
        check(s.wifiProfileId == 3, "the profile behind the active connection");
        nm.set(WIFI, I_DEV, "State", g_variant_new_uint32(120));
        nm.set(WIFI, I_DEV, "StateReason", g_variant_new("(uu)", 120, 8));
        s = NmClient::readState(bus);
        check(s.wifiStateReason == 8, "the reason, not the state, out of StateReason");
        nm.set(AP, I_AP, "HwAddress", g_variant_new_string("AA:BB:CC:DD:EE:FF"));
        nm.set(AP, I_AP, "Frequency", g_variant_new_uint32(2437));
        s = NmClient::readState(bus);
        check(s.wifiBssid == "AA:BB:CC:DD:EE:FF" && s.wifiFrequency == 2437,
              "the joined access point's address and frequency");

        nm.takeCalls();
        std::string why;
        check(NmClient::setWifiEnabled(bus, true, why), "switching the radio on succeeds");
        std::vector<std::string> calls = writes(nm);
        check(calls.size() == 1 && calls[0] == NM " Set WirelessEnabled true",
              "as a write of WirelessEnabled on NetworkManager");
        check(NmClient::readState(bus).wifiEnabled, "and reads back on");
        measuredLaptop(nm);
    }

    std::printf("scanning\n");
    {
        wifiScene(nm);
        nm.takeCalls();
        std::vector<NmNet::AccessPoint> found;
        std::string why;
        check(NmClient::scan(bus, found, why), "a scan succeeds");
        const std::vector<std::string> calls = nm.takeCalls();
        check(calls.size() == 1 && has(calls[0], WIFI " RequestScan"), "and asks the wifi device to scan again");
        check(found.size() == 3, "one entry per name");
        check(found.size() == 3 && found[0].ssid == "Home" && found[0].active && found[0].profileId == 3,
              "the joined network first, with its saved profile");
        check(found.size() == 3 && found[1].ssid == "Office" && found[1].strength == 90
                  && found[1].security == NmNet::kSecurityWpaPsk && found[1].profileId == 0,
              "a WPA2 network at its strongest, with no profile");
        check(found.size() == 3 && found[2].security == NmNet::kSecuritySae, "WPA3 read as SAE");

        nm.failNextCall("scan refused");
        found.clear();
        check(NmClient::scan(bus, found, why) && found.size() == 3,
              "a refused scan still answers with what NM has");
        // A profile of another type can carry a wireless group -- a hotspot
        // turned into a bond member, a hand-edited file. It is not a wifi
        // profile, and must not be offered as the saved one for that name.
        nm.set(SAVED, I_CONN, "@reply", settings("bond", "Office", "Office"));
        found.clear();
        check(NmClient::scan(bus, found, why) && found.size() == 3 && found[1].profileId == 0,
              "a profile that is not wifi is not a network's saved profile");

        nm.set(NM, I_NM, "Devices", paths({ ETH }));
        check(!NmClient::scan(bus, found, why) && why == "no wifi device", "no radio, no scan");
        measuredLaptop(nm);
    }

    std::printf("joining\n");
    {
        wifiScene(nm);
        nm.takeCalls();
        std::string why;
        int id = 0;

        NmNet::ConnectRequest saved;
        saved.profileId = 3;
        check(NmClient::connectWifi(bus, saved, id, why) && id == 3, "a saved profile by id");
        std::vector<std::string> calls = writes(nm);
        check(calls.size() == 1 && calls[0] == NM " ActivateConnection ('" SAVED_WIFI "', '" WIFI "', '/')",
              "is activated on the wifi device");

        NmNet::ConnectRequest notWifi;
        notWifi.profileId = 5;
        check(!NmClient::connectWifi(bus, notWifi, id, why) && why == "not a wifi profile",
              "a VPN profile is refused");
        check(writes(nm).empty(), "without asking NM for anything");

        NmNet::ConnectRequest wpa;
        wpa.ssid = "Office";
        wpa.securityType = "wpa-personal";
        wpa.passKey = "correct horse";
        check(NmClient::connectWifi(bus, wpa, id, why) && id == 9, "a new WPA2 network");
        calls = writes(nm);
        check(calls.size() == 1 && has(calls[0], "AddAndActivateConnection")
                  && has(calls[0], "'key-mgmt': <'wpa-psk'>") && has(calls[0], "'psk': <'correct horse'>")
                  && has(calls[0], "'type': <'802-11-wireless'>")
                  && has(calls[0], "'" WIFI "', '" AP3 "')"),
              "gets a WPA-PSK profile, on the strongest access point");

        NmNet::ConnectRequest sae = wpa;
        sae.ssid = "Modern";
        check(NmClient::connectWifi(bus, sae, id, why), "a WPA3-only network");
        calls = writes(nm);
        check(calls.size() == 1 && has(calls[0], "'key-mgmt': <'sae'>"), "is joined with SAE");

        NmNet::ConnectRequest wep;
        wep.ssid = "Attic";
        wep.securityType = "wep";
        wep.passKey = "abcde";
        wep.keyIndex = 2;
        wep.hidden = true;
        check(NmClient::connectWifi(bus, wep, id, why), "a hidden WEP network not in the scan");
        calls = writes(nm);
        check(calls.size() == 1 && has(calls[0], "'wep-key2': <'abcde'>")
                  && has(calls[0], "'wep-tx-keyidx': <uint32 2>") && has(calls[0], "'key-mgmt': <'none'>")
                  && has(calls[0], "'hidden': <true>") && has(calls[0], "'" WIFI "', '/')"),
              "gets the key in its slot, marked hidden, with no access point named");

        NmNet::ConnectRequest hiddenSeen = wpa;
        hiddenSeen.hidden = true;
        check(NmClient::connectWifi(bus, hiddenSeen, id, why), "a network typed by hand that is also in range");
        calls = writes(nm);
        check(calls.size() == 1 && has(calls[0], "'" WIFI "', '/')"),
              "still names no access point: a hidden network's beacon has no name to match");

        NmNet::ConnectRequest openButLocked;
        openButLocked.ssid = "Office";
        check(!NmClient::connectWifi(bus, openButLocked, id, why) && why == "this network needs a password",
              "joining a secured network as open is refused");
        check(writes(nm).empty(), "before NM is asked");

        NmNet::ConnectRequest openSaved;
        openSaved.ssid = "Home";
        check(NmClient::connectWifi(bus, openSaved, id, why) && id == 3,
              "an open network with a saved profile");
        calls = writes(nm);
        check(calls.size() == 1 && has(calls[0], "ActivateConnection ('" SAVED_WIFI "'"),
              "reuses the profile instead of making another");

        nm.set(SAVED_WIFI, I_CONN, "@reply", settings("802-11-wireless", "Home", "Home", "wpa-psk", "manual"));
        NmNet::ConnectRequest newKey;
        newKey.ssid = "Home";
        newKey.securityType = "wpa-personal";
        newKey.passKey = "new password";
        check(NmClient::connectWifi(bus, newKey, id, why) && id == 3, "a saved network with a new password");
        calls = writes(nm);
        check(calls.size() == 2 && has(calls[0], SAVED_WIFI " Update")
                  && has(calls[0], "'psk': <'new password'>") && has(calls[0], "'method': <'manual'>")
                  && has(calls[1], "ActivateConnection ('" SAVED_WIFI "'"),
              "updates the profile's key, keeps its other settings, then joins");

        nm.failNextCall("Connection activation failed");
        check(!NmClient::connectWifi(bus, saved, id, why) && has(why, "Connection activation failed"),
              "NM's refusal comes back in its own words");
        measuredLaptop(nm);
    }


    std::printf("enterprise and address settings\n");
    {
        wifiScene(nm);
        accessPoint(nm, AP4, "Corp", 70, 1, 0, 0x200);
        nm.takeCalls();
        std::string why;
        int id = 0;

        NmNet::ConnectRequest peap;
        peap.ssid = "Corp";
        peap.securityType = "enterprise";
        peap.eapType = "eapPeap";
        peap.userId = "gach";
        peap.password = "s3cret";
        check(NmClient::connectWifi(bus, peap, id, why), "a PEAP network");
        std::vector<std::string> calls = writes(nm);
        check(calls.size() == 1 && has(calls[0], "'key-mgmt': <'wpa-eap'>")
                  && has(calls[0], "'802-1x': {'eap': <['peap']>, 'identity': <'gach'>, 'password': <'s3cret'>, 'phase2-auth': <'mschapv2'>, 'system-ca-certs': <true>}")
                  && !has(calls[0], "'psk'"),
              "gets an 802-1x login, checked against the system's CAs");

        NmNet::ConnectRequest tls = peap;
        tls.eapType = "eapTls";
        tls.password.clear();
        tls.verifyServerCert = false;
        tls.clientCertificatePath = "/certs/me.pem";
        check(NmClient::connectWifi(bus, tls, id, why), "a TLS network");
        calls = writes(nm);
        check(calls.size() == 1 && has(calls[0], "'eap': <['tls']>")
                  && has(calls[0], "'client-cert': <b'file:///certs/me.pem'>")
                  && has(calls[0], "'private-key': <b'file:///certs/me.pem'>")
                  && has(calls[0], "'private-key-password-flags': <uint32 4>")
                  && !has(calls[0], "'password'") && !has(calls[0], "system-ca-certs"),
              "logs in with the certificate file, and checks no server when told not to");

        NmNet::ConnectRequest fast = peap;
        fast.eapType = "eapFast";
        check(NmClient::connectWifi(bus, fast, id, why), "a FAST network");
        calls = writes(nm);
        check(calls.size() == 1 && has(calls[0], "'phase1-fast-provisioning': <'3'>"),
              "provisions its PAC");

        nm.set(SAVED_WIFI, I_CONN, "@reply", settings("802-11-wireless", "Home", "Home", "wpa-psk"));
        NmNet::ConnectRequest move = peap;
        move.ssid = "Home";
        check(NmClient::connectWifi(bus, move, id, why) && id == 3, "a saved network moving to enterprise");
        calls = writes(nm);
        check(calls.size() == 2 && has(calls[0], SAVED_WIFI " Update") && has(calls[0], "'key-mgmt': <'wpa-eap'>")
                  && has(calls[0], "'802-1x'") && has(calls[0], "'802-11-wireless': {'ssid'"),
              "gets its security and its login replaced, the rest kept");

        NmNet::ConnectRequest manual;
        manual.profileId = 3;
        manual.addressChange = true;
        manual.staticIp = true;
        manual.ip = "192.168.1.20";
        manual.subnet = "255.255.255.0";
        manual.gateway = "192.168.1.1";
        manual.dns1 = "1.1.1.1";
        // Not a palindrome, so the byte order shows: NetworkManager's "dns" is
        // in network order, read here as a little-endian integer.
        manual.dns2 = "8.8.4.4";
        check(NmClient::connectWifi(bus, manual, id, why) && id == 3, "static addresses on a saved network");
        calls = writes(nm);
        check(calls.size() == 2 && has(calls[0], SAVED_WIFI " Update")
                  && has(calls[0], "'ipv4': {'method': <'manual'>, 'address-data': <[{'address': <'192.168.1.20'>, 'prefix': <uint32 24>}]>, 'gateway': <'192.168.1.1'>, 'dns': <[uint32 16843009, 67373064]>}")
                  && has(calls[0], "'802-11-wireless-security'") && has(calls[1], "ActivateConnection ('" SAVED_WIFI "'"),
              "replace its ipv4, keep the rest, and bring it up again");

        nm.set(SAVED_WIFI, I_CONN, "@reply", settings("802-11-wireless", "Home", "Home", nullptr, "manual"));
        NmNet::ConnectRequest dhcp;
        dhcp.profileId = 3;
        dhcp.addressChange = true;
        check(NmClient::connectWifi(bus, dhcp, id, why), "back to DHCP");
        calls = writes(nm);
        check(calls.size() == 2 && has(calls[0], "'ipv4': {'method': <'auto'>}") && !has(calls[0], "manual"),
              "is an automatic ipv4 and nothing more");

        NmNet::ConnectRequest vpn = dhcp;
        vpn.profileId = 5;
        check(!NmClient::connectWifi(bus, vpn, id, why) && why == "not a wifi profile" && writes(nm).empty(),
              "the VPN's addresses are not touched");
        measuredLaptop(nm);
    }
    std::printf("profiles\n");
    {
        wifiScene(nm);
        nm.set(SAVED_WIFI, I_CONN, "@reply", settings("802-11-wireless", "Home", "Home", "sae"));
        nm.takeCalls();
        NmNet::Profile profile;
        NmNet::IpInfo ip;
        bool active = false;
        std::string why;
        check(NmClient::getProfile(bus, 3, profile, ip, active, why), "the saved wifi profile is read");
        check(profile.ssid == "Home" && profile.security == NmNet::kSecuritySae && !profile.staticIp,
              "its name, its security, DHCP");
        check(active && ip.ip == "192.168.1.66" && ip.subnet == "255.255.255.0",
              "in use, with the device's address and mask");
        check(ip.gateway == "192.168.1.1" && ip.dns1 == "1.1.1.1" && ip.dns2 == "9.9.9.9",
              "its gateway and the first two name servers");

        nm.set(AC_WIFI, I_ACTIVE, "Connection", g_variant_new_object_path(SAVED));
        check(NmClient::getProfile(bus, 3, profile, ip, active, why) && !active && ip.ip.empty(),
              "not in use, no address");

        check(!NmClient::getProfile(bus, 5, profile, ip, active, why) && why == "not a wifi profile",
              "the VPN is not handed out");
        check(!NmClient::getProfile(bus, 42, profile, ip, active, why), "nor a profile that does not exist");

        check(!NmClient::deleteProfile(bus, 7, why) && why == "not a wifi profile",
              "the cable's profile is not deleted");
        check(!NmClient::deleteProfile(bus, 0, why), "nor profile 0");
        check(writes(nm).empty(), "and NM is not asked to delete anything");
        check(NmClient::deleteProfile(bus, 3, why), "a wifi profile is deleted");
        const std::vector<std::string> calls = writes(nm);
        check(calls.size() == 1 && calls[0] == SAVED_WIFI " Delete ()", "on its own object");

        std::vector<NmNet::Profile> saved;
        check(NmClient::listProfiles(bus, saved, why) && saved.size() == 1 && saved[0].ssid == "Home"
                  && saved[0].profileId == 3 && saved[0].security == NmNet::kSecuritySae,
              "only the wifi profiles are listed, with their security");

        std::string mac;
        check(NmClient::wifiMacAddress(bus, mac, why) && mac == "7C:21:4A:00:11:22", "the radio's address");
        measuredLaptop(nm);
    }

    std::printf("the machine going to sleep\n");
    {
        std::vector<bool> heard;
        bool heldWhenHeard = false;
        SleepWatch* watchPtr = nullptr;
        SleepWatch watch(bus, [&](bool sleeping) {
            heard.push_back(sleeping);
            heldWhenHeard = watchPtr->holding();
        });
        watchPtr = &watch;
        check(!watch.holding() && nm.heldInhibitors() == 0, "nothing is held until asked");
        check(watch.hold(true) && watch.holding(), "a delay lock is taken");
        std::vector<std::string> calls = nm.takeCalls();
        check(calls.size() == 1
                  && calls[0] == "/org/freedesktop/login1 Inhibit ('sleep', 'webOS', 'Switching Wi-Fi off before sleep', 'delay')",
              "as a sleep delay inhibitor");
        check(nm.heldInhibitors() == 1, "and logind sees it held");
        check(watch.hold(true) && nm.takeCalls().empty() && nm.heldInhibitors() == 1,
              "taking it again keeps the one");

        nm.emitPrepareForSleep(true);
        const auto pump = [&](size_t n) {
            for (int i = 0; i < 200 && heard.size() < n; ++i)
                g_main_context_iteration(nullptr, TRUE);
        };
        pump(1);
        check(heard.size() == 1 && heard[0], "going to sleep is heard");
        check(heldWhenHeard, "while the lock is still held, so there is time to act");
        watch.hold(false);
        check(!watch.holding() && nm.heldInhibitors() == 0, "releasing it lets the machine sleep");

        nm.emitPrepareForSleep(false);
        pump(2);
        check(heard.size() == 2 && !heard[1], "waking up is heard");
    }
    std::printf("a watch without a bus\n");
    {
        SleepWatch idle(nullptr, [](bool) {});
        check(!idle.hold(true) && !idle.holding(), "holds nothing");
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
