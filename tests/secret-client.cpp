// What com.palm.keymanager reads from and writes to the Secret Service, and
// what it asks of it, against a fake org.freedesktop.secrets on a private bus.
//
// tests/key-store.cpp checks what a request becomes on the webOS bus. This
// checks the other half: that a store, a fetch, a remove and an existence check
// go to the host keyring the way the accounts KeyStore needs -- the blob kept
// under a keyname attribute within our schema, replaced on a second store,
// found again on fetch, gone after a remove, and told apart from another
// collection's item that happens to share a keyname.
//
// The fake is served from its own thread on its own connection, because the
// code under test makes blocking calls: served from the calling thread, every
// call would wait for a reply that thread could never send. It declares each
// method with the signature the Secret Service has, so a call built with the
// wrong argument types is refused here the way the real daemon would refuse it.
#include "secret_client.h"

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

static const char kIntrospection[] = R"XML(
<node>
  <interface name="org.freedesktop.Secret.Service">
    <method name="OpenSession">
      <arg name="algorithm" type="s" direction="in"/>
      <arg name="input" type="v" direction="in"/>
      <arg name="output" type="v" direction="out"/>
      <arg name="result" type="o" direction="out"/>
    </method>
    <method name="ReadAlias">
      <arg name="name" type="s" direction="in"/>
      <arg name="collection" type="o" direction="out"/>
    </method>
    <method name="Unlock">
      <arg name="objects" type="ao" direction="in"/>
      <arg name="unlocked" type="ao" direction="out"/>
      <arg name="prompt" type="o" direction="out"/>
    </method>
  </interface>
  <interface name="org.freedesktop.Secret.Collection">
    <method name="CreateItem">
      <arg name="properties" type="a{sv}" direction="in"/>
      <arg name="secret" type="(oayays)" direction="in"/>
      <arg name="replace" type="b" direction="in"/>
      <arg name="item" type="o" direction="out"/>
      <arg name="prompt" type="o" direction="out"/>
    </method>
    <method name="SearchItems">
      <arg name="attributes" type="a{ss}" direction="in"/>
      <arg name="results" type="ao" direction="out"/>
    </method>
  </interface>
  <interface name="org.freedesktop.Secret.Item">
    <method name="GetSecret">
      <arg name="session" type="o" direction="in"/>
      <arg name="secret" type="(oayays)" direction="out"/>
    </method>
    <method name="Delete">
      <arg name="prompt" type="o" direction="out"/>
    </method>
  </interface>
  <interface name="org.freedesktop.Secret.Session">
    <method name="Close"/>
  </interface>
</node>
)XML";

#define SECRETS "/org/freedesktop/secrets"
#define COLLECTION SECRETS "/collection/login"
#define SESSION SECRETS "/session/s1"

#define I_SERVICE "org.freedesktop.Secret.Service"
#define I_COLLECTION "org.freedesktop.Secret.Collection"
#define I_ITEM "org.freedesktop.Secret.Item"
#define I_SESSION "org.freedesktop.Secret.Session"

// The fake keyring. One collection, a counter for item paths, and a table from
// item path to (keyname attribute, schema attribute, blob). What the real
// daemon persists; here it lives for the length of the test.
class FakeSecrets {
public:
    explicit FakeSecrets(const std::string& address)
        : m_address(address)
    {
        m_thread = std::thread([this] { serve(); });
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_ready; });
    }

    ~FakeSecrets()
    {
        g_main_loop_quit(m_loop);
        m_thread.join();
    }

    // ReadAlias("default") answers this. The empty string stands for "no
    // default collection yet", which is a session keyring before its first
    // store; the client treats that as "nothing is there".
    void setDefaultCollection(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_defaultCollection = path;
    }

    // The next CreateItem/GetSecret/Delete fails, the way a locked collection
    // or a broken keyring would.
    void failNextCall(const std::string& message)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_failWith = message;
    }

    int itemCount()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return static_cast<int>(m_items.size());
    }

private:
    struct Item {
        std::string keyname;
        std::string schema;
        std::string blob;
    };

    static void methodCall(GDBusConnection*, const gchar*, const gchar* path,
                           const gchar* iface, const gchar* method, GVariant* params,
                           GDBusMethodInvocation* invocation, gpointer self)
    {
        static_cast<FakeSecrets*>(self)->handle(path, iface, method, params, invocation);
    }

    void handle(const std::string& path, const std::string& iface, const std::string& method,
                GVariant* params, GDBusMethodInvocation* invocation)
    {
        if (iface == I_SERVICE && method == "OpenSession") {
            GVariant* out = g_variant_new_variant(g_variant_new_string(""));
            g_dbus_method_invocation_return_value(
                invocation, g_variant_new("(vo)", out, SESSION));
            return;
        }
        if (iface == I_SERVICE && method == "ReadAlias") {
            std::string def;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                def = m_defaultCollection;
            }
            g_dbus_method_invocation_return_value(
                invocation, g_variant_new("(o)", def.empty() ? "/" : def.c_str()));
            return;
        }
        if (iface == I_SERVICE && method == "Unlock") {
            // Unlock the asked-for objects without a prompt, as a headless
            // keyring unlocked by login does.
            GVariantBuilder unlocked;
            g_variant_builder_init(&unlocked, G_VARIANT_TYPE("ao"));
            GVariantIter* iter = nullptr;
            g_variant_get(params, "(ao)", &iter);
            const char* obj = nullptr;
            while (g_variant_iter_next(iter, "&o", &obj))
                g_variant_builder_add(&unlocked, "o", obj);
            g_variant_iter_free(iter);
            g_dbus_method_invocation_return_value(
                invocation, g_variant_new("(@aoo)", g_variant_builder_end(&unlocked), "/"));
            return;
        }
        if (iface == I_SESSION && method == "Close") {
            g_dbus_method_invocation_return_value(invocation, nullptr);
            return;
        }

        // The calls that can be programmed to fail.
        {
            std::string failWith;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                failWith.swap(m_failWith);
            }
            if (!failWith.empty()) {
                g_dbus_method_invocation_return_dbus_error(
                    invocation, "org.freedesktop.Secret.Error.IsLocked", failWith.c_str());
                return;
            }
        }

        if (iface == I_COLLECTION && method == "CreateItem") {
            createItem(params, invocation);
            return;
        }
        if (iface == I_COLLECTION && method == "SearchItems") {
            searchItems(params, invocation);
            return;
        }
        if (iface == I_ITEM && method == "GetSecret") {
            getSecret(path, invocation);
            return;
        }
        if (iface == I_ITEM && method == "Delete") {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_items.erase(path);
            }
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(o)", "/"));
            return;
        }
        g_dbus_method_invocation_return_dbus_error(invocation, "org.test.Error", "unhandled");
    }

    // Read the a{ss} attributes argument into keyname + schema.
    static void readAttributes(GVariant* dict, std::string& keyname, std::string& schema)
    {
        GVariantIter* iter = nullptr;
        g_variant_get(dict, "a{ss}", &iter);
        const char* k = nullptr;
        const char* v = nullptr;
        while (g_variant_iter_next(iter, "{&s&s}", &k, &v)) {
            if (std::string(k) == "keyname")
                keyname = v;
            else if (std::string(k) == "xdg:schema")
                schema = v;
        }
        g_variant_iter_free(iter);
    }

    void createItem(GVariant* params, GDBusMethodInvocation* invocation)
    {
        GVariant* props = nullptr;
        GVariant* secret = nullptr;
        gboolean replace = FALSE;
        g_variant_get(params, "(@a{sv}@(oayays)b)", &props, &secret, &replace);

        // The attributes live under the Item.Attributes property in the dict.
        std::string keyname, schema;
        GVariant* attrs = g_variant_lookup_value(props, "org.freedesktop.Secret.Item.Attributes",
                                                 G_VARIANT_TYPE("a{ss}"));
        if (attrs) {
            readAttributes(attrs, keyname, schema);
            g_variant_unref(attrs);
        }

        // The value is the third field of the (oayays) struct.
        const gchar* sess = nullptr;
        GVariant* par = nullptr;
        GVariant* value = nullptr;
        const gchar* ctype = nullptr;
        g_variant_get(secret, "(&o@ay@ay&s)", &sess, &par, &value, &ctype);
        gsize length = 0;
        const guchar* bytes = static_cast<const guchar*>(
            g_variant_get_fixed_array(value, &length, sizeof(guchar)));
        const std::string blob(reinterpret_cast<const char*>(bytes), length);
        g_variant_unref(par);
        g_variant_unref(value);
        g_variant_unref(props);
        g_variant_unref(secret);

        std::string itemPath;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            // replace=true: a store under a keyname that already exists
            // overwrites it rather than adding a second item.
            if (replace) {
                for (auto& entry : m_items) {
                    if (entry.second.keyname == keyname && entry.second.schema == schema) {
                        entry.second.blob = blob;
                        itemPath = entry.first;
                        break;
                    }
                }
            }
            if (itemPath.empty()) {
                itemPath = std::string(COLLECTION) + "/" + std::to_string(++m_nextItem);
                m_items[itemPath] = { keyname, schema, blob };
            }
        }
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(oo)", itemPath.c_str(), "/"));
    }

    void searchItems(GVariant* params, GDBusMethodInvocation* invocation)
    {
        GVariant* attrs = nullptr;
        g_variant_get(params, "(@a{ss})", &attrs);
        std::string keyname, schema;
        readAttributes(attrs, keyname, schema);
        g_variant_unref(attrs);

        GVariantBuilder results;
        g_variant_builder_init(&results, G_VARIANT_TYPE("ao"));
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& entry : m_items)
                if (entry.second.keyname == keyname && entry.second.schema == schema)
                    g_variant_builder_add(&results, "o", entry.first.c_str());
        }
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new("(@ao)", g_variant_builder_end(&results)));
    }

    void getSecret(const std::string& path, GDBusMethodInvocation* invocation)
    {
        std::string blob;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_items.find(path);
            if (it != m_items.end()) {
                blob = it->second.blob;
                found = true;
            }
        }
        if (!found) {
            g_dbus_method_invocation_return_dbus_error(
                invocation, "org.freedesktop.Secret.Error.NoSuchObject", "no item");
            return;
        }
        GVariantBuilder params;
        g_variant_builder_init(&params, G_VARIANT_TYPE("ay"));
        GVariantBuilder value;
        g_variant_builder_init(&value, G_VARIANT_TYPE("ay"));
        for (unsigned char c : blob)
            g_variant_builder_add(&value, "y", c);
        GVariant* secret = g_variant_new("(o@ay@ays)", SESSION,
                                         g_variant_builder_end(&params),
                                         g_variant_builder_end(&value), "text/plain");
        g_dbus_method_invocation_return_value(invocation, g_variant_new_tuple(&secret, 1));
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

        // The item objects are registered as a subtree, so an item path created
        // at run time still has a handler. The three fixed objects are
        // registered directly.
        const std::vector<std::pair<const char*, const char*>> objects = {
            { SECRETS, I_SERVICE },
            { COLLECTION, I_COLLECTION },
            { SESSION, I_SESSION },
        };
        for (const auto& object : objects) {
            g_dbus_connection_register_object(
                bus, object.first, g_dbus_node_info_lookup_interface(node, object.second),
                &vtable, this, nullptr, &error);
            g_assert_no_error(error);
        }

        // Items live under the collection; register a subtree so any
        // .../collection/login/<n> resolves to the Item interface.
        GDBusSubtreeVTable subtree = { subtreeEnumerate, subtreeIntrospect, subtreeDispatch };
        g_dbus_connection_register_subtree(bus, COLLECTION, &subtree,
                                           G_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES,
                                           this, nullptr, &error);
        g_assert_no_error(error);

        GVariant* reply = g_dbus_connection_call_sync(
            bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
            "RequestName", g_variant_new("(su)", "org.freedesktop.secrets", 4),
            G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &error);
        g_assert_no_error(error);
        g_variant_unref(reply);

        m_node = node;
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

    // The subtree that makes every item path answer the Item interface.
    static char** subtreeEnumerate(GDBusConnection*, const gchar*, const gchar*, gpointer)
    {
        return g_new0(char*, 1); // enumeration not needed; dispatch handles unenumerated
    }

    static GDBusInterfaceInfo** subtreeIntrospect(GDBusConnection*, const gchar*, const gchar*,
                                                  const gchar*, gpointer self)
    {
        FakeSecrets* fake = static_cast<FakeSecrets*>(self);
        GDBusInterfaceInfo** out = g_new0(GDBusInterfaceInfo*, 2);
        out[0] = g_dbus_interface_info_ref(
            g_dbus_node_info_lookup_interface(fake->m_node, I_ITEM));
        return out;
    }

    static const GDBusInterfaceVTable* subtreeDispatch(GDBusConnection*, const gchar*,
                                                       const gchar*, const gchar*,
                                                       const gchar*, gpointer* out_user_data,
                                                       gpointer self)
    {
        static const GDBusInterfaceVTable vtable = { methodCall, nullptr, nullptr, { nullptr } };
        *out_user_data = self;
        return &vtable;
    }

    std::string m_address;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_ready = false;
    GMainLoop* m_loop = nullptr;
    GDBusConnection* m_bus = nullptr;
    GDBusNodeInfo* m_node = nullptr;
    std::map<std::string, Item> m_items;
    unsigned m_nextItem = 0;
    std::string m_defaultCollection = COLLECTION;
    std::string m_failWith;
};

static void run(const char* address)
{
    GError* error = nullptr;
    GDBusConnection* bus = g_dbus_connection_new_for_address_sync(
        address,
        static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT
                                          | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, nullptr, &error);
    g_assert_no_error(error);

    FakeSecrets secrets(address);

    std::printf("a store, then a fetch, returns the blob\n");
    {
        KeyStore::StoreRequest req;
        req.keyname = "acct1";
        req.keydata = "{\"token\":\"abc\"}";
        std::string err;
        check(SecretClient::store(bus, req, err), "store succeeds");
        check(secrets.itemCount() == 1, "one item is kept");

        std::string blob;
        bool found = false;
        check(SecretClient::fetch(bus, "acct1", blob, found, err) && found, "fetch finds it");
        check(blob == "{\"token\":\"abc\"}", "the blob round-trips byte for byte");
    }

    std::printf("a second store under the same keyname replaces, not duplicates\n");
    {
        KeyStore::StoreRequest req;
        req.keyname = "acct1";
        req.keydata = "{\"token\":\"xyz\"}";
        std::string err;
        check(SecretClient::store(bus, req, err), "store succeeds");
        check(secrets.itemCount() == 1, "still one item, not two");
        std::string blob;
        bool found = false;
        SecretClient::fetch(bus, "acct1", blob, found, err);
        check(blob == "{\"token\":\"xyz\"}", "the new blob is what fetch returns");
    }

    std::printf("exists tells a present key from an absent one\n");
    {
        std::string err;
        bool present = false;
        check(SecretClient::exists(bus, "acct1", present, err) && present, "a stored key exists");
        present = true;
        check(SecretClient::exists(bus, "never-stored", present, err) && !present,
              "an unstored key does not, and that is not an error");
    }

    std::printf("a fetch of a missing key is a clean not-found, not a crash\n");
    {
        std::string blob = "sentinel";
        bool found = true;
        std::string err;
        const bool ok = SecretClient::fetch(bus, "never-stored", blob, found, err);
        check(!ok && !found, "fetch reports not found");
        check(err == KeyStore::notFoundMessage(), "with the not-found message");
    }

    std::printf("remove deletes the item; removing nothing is still success\n");
    {
        std::string err;
        check(SecretClient::remove(bus, "acct1", err), "remove succeeds");
        check(secrets.itemCount() == 0, "the item is gone");
        bool present = true;
        SecretClient::exists(bus, "acct1", present, err);
        check(!present, "and no longer exists");
        // putCredentials removes before its first store, when nothing is there.
        check(SecretClient::remove(bus, "acct1", err), "removing an absent key is not an error");
    }

    std::printf("a keyring error on a store is reported, not swallowed\n");
    {
        secrets.failNextCall("Cannot create an item in a locked collection");
        KeyStore::StoreRequest req;
        req.keyname = "acct2";
        req.keydata = "x";
        std::string err;
        check(!SecretClient::store(bus, req, err), "a failing CreateItem fails the store");
        check(!err.empty(), "with the keyring's own message");
    }

    std::printf("no default collection yet is a clean empty store, not a failure\n");
    {
        secrets.setDefaultCollection("");
        std::string err;
        bool present = true;
        check(SecretClient::exists(bus, "acct1", present, err) && !present,
              "exists on an empty keyring says absent, not error");
        std::string blob;
        bool found = true;
        check(!SecretClient::fetch(bus, "acct1", blob, found, err) && !found,
              "fetch on an empty keyring is not found");
        check(SecretClient::remove(bus, "acct1", err), "remove on an empty keyring is success");
        secrets.setDefaultCollection(COLLECTION);
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
