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

#include "secret_client.h"

#include <vector>

namespace SecretClient {

namespace {

const char kService[] = "org.freedesktop.secrets";
const char kServicePath[] = "/org/freedesktop/secrets";
const char kServiceIface[] = "org.freedesktop.Secret.Service";
const char kCollectionIface[] = "org.freedesktop.Secret.Collection";
const char kItemIface[] = "org.freedesktop.Secret.Item";
const char kPropsIface[] = "org.freedesktop.DBus.Properties";

// The default collection, by its well-known alias. ReadAlias("default") returns
// the object path of whichever collection the keyring treats as default, which
// is where a desktop keeps its login secrets.
const char kDefaultAlias[] = "default";

const int kTimeoutMs = 5000;

std::string errorFrom(GError* error, const char* fallback)
{
    std::string out = (error && error->message && *error->message) ? error->message : fallback;
    g_clear_error(&error);
    return out;
}

GVariant* call(GDBusConnection* bus, const char* path, const char* iface,
               const char* method, GVariant* args, const GVariantType* reply,
               std::string& error)
{
    if (!bus) {
        error = "no session bus";
        if (args)
            g_variant_unref(g_variant_ref_sink(args));
        return nullptr;
    }
    GError* gerror = nullptr;
    GVariant* out = g_dbus_connection_call_sync(
        bus, kService, path, iface, method, args, reply,
        G_DBUS_CALL_FLAGS_NONE, kTimeoutMs, nullptr, &gerror);
    if (!out)
        error = errorFrom(gerror, "secret service did not answer");
    return out;
}

std::string objectPathOut(GVariant* reply)
{
    const char* path = nullptr;
    g_variant_get(reply, "(&o)", &path);
    return path ? path : std::string();
}

// The default collection's path. The empty string means the keyring reports no
// default, which a session keyring created on the fly does until something is
// stored; callers treat that as "nothing is there".
std::string defaultCollection(GDBusConnection* bus, std::string& error)
{
    GVariant* reply = call(bus, kServicePath, kServiceIface, "ReadAlias",
                           g_variant_new("(s)", kDefaultAlias), G_VARIANT_TYPE("(o)"), error);
    if (!reply)
        return std::string();
    std::string path = objectPathOut(reply);
    g_variant_unref(reply);
    if (path == "/")
        path.clear();
    return path;
}

// The attribute dictionary that pins an item to our schema and a keyname.
// a{ss}, as the Secret Service wants.
GVariant* attributesFor(const std::string& keyname)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{ss}"));
    g_variant_builder_add(&builder, "{ss}", "xdg:schema", KeyStore::schema());
    g_variant_builder_add(&builder, "{ss}", KeyStore::keynameAttribute(), keyname.c_str());
    return g_variant_builder_end(&builder);
}

// Items in the default collection whose attributes match keyname within our
// schema. The service's SearchItems searches every unlocked collection; the
// collection's own SearchItems stays within it, which is what keeps us from
// reading another collection's item that happens to share a keyname.
std::vector<std::string> searchItems(GDBusConnection* bus, const std::string& collection,
                                     const std::string& keyname, std::string& error)
{
    std::vector<std::string> paths;
    if (collection.empty())
        return paths;
    GVariant* reply = call(bus, collection.c_str(), kCollectionIface, "SearchItems",
                           g_variant_new("(@a{ss})", attributesFor(keyname)),
                           G_VARIANT_TYPE("(ao)"), error);
    if (!reply)
        return paths;
    GVariantIter* iter = nullptr;
    g_variant_get(reply, "(ao)", &iter);
    const char* path = nullptr;
    while (g_variant_iter_next(iter, "&o", &path))
        paths.emplace_back(path);
    g_variant_iter_free(iter);
    g_variant_unref(reply);
    return paths;
}

// Unlock a path (collection or item) if the service will do it without a
// prompt. A prompt object path (not "/") is returned when the service wants the
// user; we do not drive prompts -- a headless keyring on a desktop is already
// unlocked by login -- so a prompt reads as "could not unlock here", left to
// the caller to treat as a locked store rather than an error.
bool unlock(GDBusConnection* bus, const std::string& path, std::string& error)
{
    if (path.empty())
        return false;
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("ao"));
    g_variant_builder_add(&builder, "o", path.c_str());
    GVariant* reply = call(bus, kServicePath, kServiceIface, "Unlock",
                           g_variant_new("(ao)", &builder), G_VARIANT_TYPE("(aoo)"), error);
    if (!reply)
        return false;
    GVariantIter* unlocked = nullptr;
    const char* prompt = nullptr;
    g_variant_get(reply, "(aoo)", &unlocked, &prompt);
    bool anyUnlocked = g_variant_iter_n_children(unlocked) > 0;
    const bool prompted = prompt && g_strcmp0(prompt, "/") != 0;
    g_variant_iter_free(unlocked);
    g_variant_unref(reply);
    return anyUnlocked && !prompted;
}

} // namespace

bool openSession(GDBusConnection* bus, Session& session, std::string& error)
{
    session.bus = bus;
    session.open = false;
    session.path.clear();

    // "plain": the secret value crosses the bus unencrypted. The bus is the
    // user's own session socket; the alternative (dh-ietf1024-sha256-aes128-
    // cbc-pkcs7) guards against another local user reading the socket, which a
    // single-user desktop session does not have.
    GVariant* reply = call(bus, kServicePath, kServiceIface, "OpenSession",
                           g_variant_new("(sv)", "plain", g_variant_new_string("")),
                           G_VARIANT_TYPE("(vo)"), error);
    if (!reply)
        return false;
    GVariant* output = nullptr;
    const char* path = nullptr;
    g_variant_get(reply, "(vo)", &output, &path);
    if (path)
        session.path = path;
    if (output)
        g_variant_unref(output);
    g_variant_unref(reply);
    session.open = !session.path.empty();
    if (!session.open)
        error = "secret service opened no session";
    return session.open;
}

void closeSession(Session& session)
{
    if (session.bus && session.open && !session.path.empty()) {
        std::string ignored;
        GVariant* reply = call(session.bus, session.path.c_str(),
                               "org.freedesktop.Secret.Session", "Close",
                               nullptr, nullptr, ignored);
        if (reply)
            g_variant_unref(reply);
    }
    session.open = false;
    session.path.clear();
    session.bus = nullptr;
}

bool initialize(GDBusConnection* bus, std::string& error)
{
    Session session;
    if (!openSession(bus, session, error))
        return false;
    const std::string collection = defaultCollection(bus, error);
    // No default collection yet is not a failure: the keyring makes one on the
    // first store. An existing one is unlocked if it can be without a prompt;
    // if it needs the user, that is the keyring's own business, not an error
    // this service can or should resolve.
    if (!collection.empty())
        unlock(bus, collection, error);
    error.clear();
    closeSession(session);
    return true;
}

bool changePassword(GDBusConnection* bus, std::string& error)
{
    // The webOS passcode does not own the host keyring's password; see the
    // header. All this can honestly do is confirm the store is reachable, which
    // is initialize's job, so Security.cpp's call settles rather than retrying.
    return initialize(bus, error);
}

bool store(GDBusConnection* bus, const KeyStore::StoreRequest& request, std::string& error)
{
    Session session;
    if (!openSession(bus, session, error))
        return false;

    std::string collection = defaultCollection(bus, error);
    if (collection.empty()) {
        error = error.empty() ? "no default keyring collection" : error;
        closeSession(session);
        return false;
    }
    unlock(bus, collection, error);
    error.clear();

    // The item's properties: its label and its attributes. The label is what a
    // keyring UI shows; it is the keyname so a human reading GNOME's Seahorse
    // can tell which account a blob belongs to.
    GVariantBuilder props;
    g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&props, "{sv}", "org.freedesktop.Secret.Item.Label",
                          g_variant_new_string(request.keyname.c_str()));
    g_variant_builder_add(&props, "{sv}", "org.freedesktop.Secret.Item.Attributes",
                          attributesFor(request.keyname));

    // The secret struct (oayays): the session, empty parameters (plain
    // transport), the value bytes, and a content type. The accounts service
    // stores JSON text, so text/plain is the honest type; the bytes are the
    // blob exactly as given.
    GVariantBuilder params;
    g_variant_builder_init(&params, G_VARIANT_TYPE("ay"));
    GVariantBuilder value;
    g_variant_builder_init(&value, G_VARIANT_TYPE("ay"));
    for (unsigned char c : request.keydata)
        g_variant_builder_add(&value, "y", c);

    GVariant* secret = g_variant_new("(o@ay@ays)",
                                     session.path.c_str(),
                                     g_variant_builder_end(&params),
                                     g_variant_builder_end(&value),
                                     "text/plain");

    // replace=true: a second store under the same keyname overwrites, which is
    // what the accounts service's putCredentials relies on after its own
    // remove, and what keeps a changed password from leaving a stale item.
    GVariant* reply = call(bus, collection.c_str(), kCollectionIface, "CreateItem",
                           g_variant_new("(@a{sv}@(oayays)b)",
                                         g_variant_builder_end(&props), secret, TRUE),
                           G_VARIANT_TYPE("(oo)"), error);
    bool ok = reply != nullptr;
    if (reply)
        g_variant_unref(reply);
    closeSession(session);
    return ok;
}

bool fetch(GDBusConnection* bus, const std::string& keyname, std::string& keydata,
           bool& found, std::string& error)
{
    found = false;
    Session session;
    if (!openSession(bus, session, error))
        return false;

    const std::string collection = defaultCollection(bus, error);
    if (collection.empty()) {
        // No collection means nothing was ever stored: a clean "not found", not
        // a broken store.
        closeSession(session);
        error = KeyStore::notFoundMessage();
        return false;
    }
    unlock(bus, collection, error);
    error.clear();

    const std::vector<std::string> items = searchItems(bus, collection, keyname, error);
    if (items.empty()) {
        closeSession(session);
        error = KeyStore::notFoundMessage();
        return false;
    }

    GVariant* reply = call(bus, items.front().c_str(), kItemIface, "GetSecret",
                           g_variant_new("(o)", session.path.c_str()),
                           G_VARIANT_TYPE("((oayays))"), error);
    if (!reply) {
        closeSession(session);
        return false;
    }

    const gchar* sessionOut = nullptr;
    GVariant* params = nullptr;
    GVariant* value = nullptr;
    const gchar* contentType = nullptr;
    g_variant_get(reply, "((&o@ay@ay&s))", &sessionOut, &params, &value, &contentType);
    gsize length = 0;
    const guchar* bytes = static_cast<const guchar*>(
        g_variant_get_fixed_array(value, &length, sizeof(guchar)));
    keydata.assign(reinterpret_cast<const char*>(bytes), length);
    g_variant_unref(params);
    g_variant_unref(value);
    g_variant_unref(reply);
    closeSession(session);
    found = true;
    return true;
}

bool remove(GDBusConnection* bus, const std::string& keyname, std::string& error)
{
    Session session;
    if (!openSession(bus, session, error))
        return false;

    const std::string collection = defaultCollection(bus, error);
    if (collection.empty()) {
        // Nothing to remove is success: putCredentials removes before its first
        // store, when the collection may not exist yet.
        closeSession(session);
        error.clear();
        return true;
    }
    unlock(bus, collection, error);
    error.clear();

    const std::vector<std::string> items = searchItems(bus, collection, keyname, error);
    for (const std::string& item : items) {
        GVariant* reply = call(bus, item.c_str(), kItemIface, "Delete",
                               nullptr, G_VARIANT_TYPE("(o)"), error);
        if (!reply) {
            closeSession(session);
            return false;
        }
        g_variant_unref(reply);
    }
    closeSession(session);
    error.clear();
    return true;
}

bool exists(GDBusConnection* bus, const std::string& keyname, bool& present, std::string& error)
{
    present = false;
    Session session;
    if (!openSession(bus, session, error))
        return false;

    const std::string collection = defaultCollection(bus, error);
    if (collection.empty()) {
        closeSession(session);
        error.clear();
        return true; // reachable store, key simply absent
    }
    unlock(bus, collection, error);
    error.clear();

    const std::vector<std::string> items = searchItems(bus, collection, keyname, error);
    present = !items.empty();
    closeSession(session);
    error.clear();
    return true;
}

} // namespace SecretClient
