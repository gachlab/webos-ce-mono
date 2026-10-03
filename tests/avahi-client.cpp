// What com.palm.zeroconf asks of Avahi, and what it reads back, against a fake
// Avahi on a private bus.
//
// tests/zeroconf-records.cpp checks what a result becomes on the webOS bus.
// This checks the other half: that a resolve reads host/address/port/TXT out of
// Avahi's ResolveService, that a register runs EntryGroupNew -> AddService ->
// Commit and that unregister frees the group, and that a browse hears Avahi's
// ItemNew/ItemRemove/AllForNow signals and turns them into the added/removed/
// complete callbacks. None of it touches the host's real mDNS daemon: the fake
// is served on a private bus GTestDBus brings up.
#include "avahi_client.h"

#include <gio/gio.h>

#include <condition_variable>
#include <cstdio>
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

static const char kIntrospection[] = R"XML(
<node>
  <interface name="org.freedesktop.Avahi.Server">
    <method name="ServiceBrowserNew">
      <arg name="interface" type="i" direction="in"/>
      <arg name="protocol" type="i" direction="in"/>
      <arg name="type" type="s" direction="in"/>
      <arg name="domain" type="s" direction="in"/>
      <arg name="flags" type="u" direction="in"/>
      <arg name="browser" type="o" direction="out"/>
    </method>
    <method name="ResolveService">
      <arg name="interface" type="i" direction="in"/>
      <arg name="protocol" type="i" direction="in"/>
      <arg name="name" type="s" direction="in"/>
      <arg name="type" type="s" direction="in"/>
      <arg name="domain" type="s" direction="in"/>
      <arg name="aprotocol" type="i" direction="in"/>
      <arg name="flags" type="u" direction="in"/>
      <arg name="r_interface" type="i" direction="out"/>
      <arg name="r_protocol" type="i" direction="out"/>
      <arg name="r_name" type="s" direction="out"/>
      <arg name="r_type" type="s" direction="out"/>
      <arg name="r_domain" type="s" direction="out"/>
      <arg name="host" type="s" direction="out"/>
      <arg name="r_aprotocol" type="i" direction="out"/>
      <arg name="address" type="s" direction="out"/>
      <arg name="port" type="q" direction="out"/>
      <arg name="txt" type="aay" direction="out"/>
      <arg name="r_flags" type="u" direction="out"/>
    </method>
    <method name="EntryGroupNew">
      <arg name="group" type="o" direction="out"/>
    </method>
  </interface>
  <interface name="org.freedesktop.Avahi.ServiceBrowser">
    <method name="Free"/>
    <signal name="ItemNew">
      <arg name="interface" type="i"/>
      <arg name="protocol" type="i"/>
      <arg name="name" type="s"/>
      <arg name="type" type="s"/>
      <arg name="domain" type="s"/>
      <arg name="flags" type="u"/>
    </signal>
    <signal name="ItemRemove">
      <arg name="interface" type="i"/>
      <arg name="protocol" type="i"/>
      <arg name="name" type="s"/>
      <arg name="type" type="s"/>
      <arg name="domain" type="s"/>
      <arg name="flags" type="u"/>
    </signal>
    <signal name="AllForNow"/>
    <signal name="Failure"><arg name="error" type="s"/></signal>
  </interface>
  <interface name="org.freedesktop.Avahi.EntryGroup">
    <method name="AddService">
      <arg name="interface" type="i" direction="in"/>
      <arg name="protocol" type="i" direction="in"/>
      <arg name="flags" type="u" direction="in"/>
      <arg name="name" type="s" direction="in"/>
      <arg name="type" type="s" direction="in"/>
      <arg name="domain" type="s" direction="in"/>
      <arg name="host" type="s" direction="in"/>
      <arg name="port" type="q" direction="in"/>
      <arg name="txt" type="aay" direction="in"/>
    </method>
    <method name="Commit"/>
    <method name="Reset"/>
    <method name="Free"/>
  </interface>
</node>
)XML";

#define SERVER "/"
#define BROWSER "/Client0/ServiceBrowser0"
#define GROUP "/Client0/EntryGroup0"

#define I_SERVER "org.freedesktop.Avahi.Server"
#define I_BROWSER "org.freedesktop.Avahi.ServiceBrowser"
#define I_GROUP "org.freedesktop.Avahi.EntryGroup"

class FakeAvahi {
public:
    explicit FakeAvahi(const std::string& address)
        : m_address(address)
    {
        m_thread = std::thread([this] { serve(); });
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_ready; });
    }

    ~FakeAvahi()
    {
        g_main_loop_quit(m_loop);
        m_thread.join();
    }

    // Emit one of the browser's signals, as the daemon would as the cache fills.
    void emitItemNew(const char* name, const char* type, const char* domain)
    {
        emit("ItemNew", g_variant_new("(iisssu)", 0, 0, name, type, domain, 0u));
    }
    void emitItemRemove(const char* name, const char* type, const char* domain)
    {
        emit("ItemRemove", g_variant_new("(iisssu)", 0, 0, name, type, domain, 0u));
    }
    void emitAllForNow() { emit("AllForNow", nullptr); }

    std::vector<std::string> takeCalls()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::string> out;
        out.swap(m_calls);
        return out;
    }

private:
    void emit(const char* name, GVariant* params)
    {
        GDBusConnection* bus = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            bus = m_bus;
        }
        g_dbus_connection_emit_signal(bus, nullptr, BROWSER, I_BROWSER, name, params, nullptr);
        g_dbus_connection_flush_sync(bus, nullptr, nullptr);
    }

    static void methodCall(GDBusConnection*, const gchar*, const gchar* path,
                           const gchar* iface, const gchar* method, GVariant* params,
                           GDBusMethodInvocation* invocation, gpointer self)
    {
        static_cast<FakeAvahi*>(self)->handle(path, iface, method, params, invocation);
    }

    void handle(const std::string& path, const std::string& iface, const std::string& method,
                GVariant* params, GDBusMethodInvocation* invocation)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_calls.push_back(path + " " + method);
        }
        if (iface == I_SERVER && method == "ServiceBrowserNew") {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(o)", BROWSER));
            return;
        }
        if (iface == I_SERVER && method == "EntryGroupNew") {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(o)", GROUP));
            return;
        }
        if (iface == I_SERVER && method == "ResolveService") {
            resolveService(params, invocation);
            return;
        }
        if (iface == I_BROWSER && method == "Free") {
            g_dbus_method_invocation_return_value(invocation, nullptr);
            return;
        }
        if (iface == I_GROUP && (method == "AddService" || method == "Commit"
                                 || method == "Reset" || method == "Free")) {
            g_dbus_method_invocation_return_value(invocation, nullptr);
            return;
        }
        g_dbus_method_invocation_return_dbus_error(invocation, "org.test.Error", "unhandled");
    }

    void resolveService(GVariant* params, GDBusMethodInvocation* invocation)
    {
        const gchar* name = nullptr;
        const gchar* type = nullptr;
        const gchar* domain = nullptr;
        g_variant_get(params, "(ii&s&s&siu)", nullptr, nullptr, &name, &type, &domain,
                      nullptr, nullptr);
        // Resolve only the one instance the test publishes; anything else is a
        // name that does not resolve.
        if (std::string(name ? name : "") != "Office Printer") {
            g_dbus_method_invocation_return_dbus_error(
                invocation, "org.freedesktop.Avahi.TimeoutError", "not found");
            return;
        }
        GVariantBuilder txt;
        g_variant_builder_init(&txt, G_VARIANT_TYPE("aay"));
        for (const char* entry : { "rp=ipp/print", "ty=HP LaserJet" }) {
            GVariantBuilder bytes;
            g_variant_builder_init(&bytes, G_VARIANT_TYPE("ay"));
            for (const char* p = entry; *p; ++p)
                g_variant_builder_add(&bytes, "y", (guchar)*p);
            g_variant_builder_add_value(&txt, g_variant_builder_end(&bytes));
        }
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new("(iissssisq@aayu)", 0, 0, name, type, domain,
                          "printer.local", 0, "192.168.1.20", (guint16)631,
                          g_variant_builder_end(&txt), 0u));
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
        const GDBusInterfaceVTable vtable = { methodCall, nullptr, nullptr, { nullptr } };
        const std::vector<std::pair<const char*, const char*>> objects = {
            { SERVER, I_SERVER },
            { BROWSER, I_BROWSER },
            { GROUP, I_GROUP },
        };
        for (const auto& object : objects) {
            g_dbus_connection_register_object(
                bus, object.first, g_dbus_node_info_lookup_interface(node, object.second),
                &vtable, this, nullptr, &error);
            g_assert_no_error(error);
        }

        GVariant* reply = g_dbus_connection_call_sync(
            bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
            "RequestName", g_variant_new("(su)", "org.freedesktop.Avahi", 4),
            G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
        g_assert_no_error(error);
        g_variant_unref(reply);

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
    GDBusConnection* m_bus = nullptr;
    std::vector<std::string> m_calls;
};

// Pump the caller's thread-default main context so queued signal callbacks run.
static void pump(int times)
{
    for (int i = 0; i < times; ++i) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        g_usleep(20000);
        while (g_main_context_iteration(nullptr, FALSE)) {}
    }
}

static bool hasCall(const std::vector<std::string>& calls, const std::string& needle)
{
    for (const std::string& c : calls)
        if (c.find(needle) != std::string::npos)
            return true;
    return false;
}

static void run(const char* address)
{
    GError* error = nullptr;
    GDBusConnection* bus = g_dbus_connection_new_for_address_sync(
        address,
        static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT
                                          | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, nullptr, &error);
    g_assert_no_error(error);

    FakeAvahi avahi(address);

    std::printf("a browse hears ItemNew/ItemRemove/AllForNow as added/removed/complete\n");
    {
        std::vector<Zeroconf::Service> last;
        std::string lastChanged;
        AvahiClient::Browser browser;
        std::string err;
        const bool ok = AvahiClient::startBrowse(
            bus, "_ipp._tcp",
            [&](const std::vector<Zeroconf::Service>& s, const char* changed) {
                last = s;
                lastChanged = changed ? changed : "";
            },
            browser, err);
        check(ok, "ServiceBrowserNew succeeds");
        check(hasCall(avahi.takeCalls(), "ServiceBrowserNew"), "the browser was asked for");

        avahi.emitItemNew("Office Printer", "_ipp._tcp", "local");
        pump(3);
        check(last.size() == 1 && last[0].name == "Office Printer", "ItemNew adds the instance");
        check(lastChanged == "added", "and reports it as added");

        avahi.emitItemNew("Lab Printer", "_ipp._tcp", "local");
        pump(3);
        check(last.size() == 2, "a second ItemNew adds another");

        // The same instance seen on a second interface is not added twice.
        avahi.emitItemNew("Office Printer", "_ipp._tcp", "local");
        pump(3);
        check(last.size() == 2, "a duplicate instance is not added again");

        avahi.emitItemRemove("Lab Printer", "_ipp._tcp", "local");
        pump(3);
        check(last.size() == 1 && last[0].name == "Office Printer", "ItemRemove drops it");
        check(lastChanged == "removed", "and reports it as removed");

        avahi.emitAllForNow();
        pump(3);
        check(lastChanged == "complete", "AllForNow is the complete signal");

        AvahiClient::stopBrowse(browser);
        check(hasCall(avahi.takeCalls(), "Free"), "stopping the browse frees the browser");
    }

    std::printf("a resolve reads host, address, port and TXT\n");
    {
        Zeroconf::Resolved r;
        std::string err;
        const bool ok = AvahiClient::resolve(bus, "Office Printer", "_ipp._tcp", "local", r, err);
        check(ok, "ResolveService succeeds for a known instance");
        check(r.hostname == "printer.local", "the hostname");
        check(r.address == "192.168.1.20", "the address");
        check(r.port == 631, "the port");
        check(r.txt.size() == 2 && r.txt["rp"] == "ipp/print" && r.txt["ty"] == "HP LaserJet",
              "both TXT pairs, split on =");
    }

    std::printf("a name that does not resolve is a clean failure\n");
    {
        Zeroconf::Resolved r;
        std::string err;
        const bool ok = AvahiClient::resolve(bus, "Nope", "_ipp._tcp", "local", r, err);
        check(!ok, "resolve of an unknown instance fails");
        check(!err.empty(), "with Avahi's own message");
    }

    std::printf("a register runs EntryGroupNew -> AddService -> Commit, and unregister frees it\n");
    {
        avahi.takeCalls();
        Zeroconf::Service service{ "Office Printer", "_ipp._tcp", "local" };
        AvahiClient::Entry entry;
        std::string err;
        const bool ok = AvahiClient::registerService(
            bus, service, 631, { { "rp", "ipp/print" } }, entry, err);
        check(ok, "register succeeds");
        const std::vector<std::string> calls = avahi.takeCalls();
        check(hasCall(calls, "EntryGroupNew"), "a group is created");
        check(hasCall(calls, "AddService"), "the service is added to it");
        check(hasCall(calls, "Commit"), "and committed");

        AvahiClient::unregisterService(entry);
        check(hasCall(avahi.takeCalls(), "Free"), "unregister frees the group");
    }

    g_object_unref(bus);
}

int main()
{
    GTestDBus* testBus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(testBus);
    run(g_test_dbus_get_bus_address(testBus));
    g_test_dbus_down(testBus);
    g_object_unref(testBus);

    std::printf("\n%s\n", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
