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

#include "avahi_client.h"

#include <memory>

namespace AvahiClient {

namespace {

const char kService[] = "org.freedesktop.Avahi";
const char kServerPath[] = "/";
const char kServerIface[] = "org.freedesktop.Avahi.Server";
const char kBrowserIface[] = "org.freedesktop.Avahi.ServiceBrowser";
const char kEntryGroupIface[] = "org.freedesktop.Avahi.EntryGroup";

// Avahi's "any interface" and "any protocol" (AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC).
const gint32 kIfaceUnspec = -1;
const gint32 kProtoUnspec = -1;
const guint32 kNoFlags = 0;

const int kTimeoutMs = 5000;

std::string errorFrom(GError* error, const char* fallback)
{
    std::string out = (error && error->message && *error->message) ? error->message : fallback;
    g_clear_error(&error);
    return out;
}

// The browse state carried through the GDBus signal callbacks. One per live
// Browser; owned by it, freed when the browser is stopped.
struct BrowseState {
    GDBusConnection* bus = nullptr;
    std::string type;
    BrowseCallback callback;
    std::vector<Zeroconf::Service> services;
};

void emitBrowse(BrowseState* state, const char* changed)
{
    if (state->callback)
        state->callback(state->services, changed);
}

void onItemNew(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
               GVariant* params, gpointer user)
{
    BrowseState* state = static_cast<BrowseState*>(user);
    gint32 iface = 0, proto = 0;
    const gchar* name = nullptr;
    const gchar* type = nullptr;
    const gchar* domain = nullptr;
    guint32 flags = 0;
    g_variant_get(params, "(ii&s&s&su)", &iface, &proto, &name, &type, &domain, &flags);
    Zeroconf::Service service{ name ? name : "", type ? type : "", domain ? domain : "" };
    // Avahi reports the same instance once per interface/protocol it is seen on;
    // webOS has no notion of that, so an instance already present by name/type/
    // domain is not added twice.
    for (const auto& existing : state->services)
        if (existing == service)
            return;
    state->services.push_back(service);
    emitBrowse(state, "added");
}

void onItemRemove(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                  GVariant* params, gpointer user)
{
    BrowseState* state = static_cast<BrowseState*>(user);
    gint32 iface = 0, proto = 0;
    const gchar* name = nullptr;
    const gchar* type = nullptr;
    const gchar* domain = nullptr;
    guint32 flags = 0;
    g_variant_get(params, "(ii&s&s&su)", &iface, &proto, &name, &type, &domain, &flags);
    Zeroconf::Service gone{ name ? name : "", type ? type : "", domain ? domain : "" };
    for (auto it = state->services.begin(); it != state->services.end(); ++it) {
        if (*it == gone) {
            state->services.erase(it);
            emitBrowse(state, "removed");
            return;
        }
    }
}

void onAllForNow(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
                 GVariant*, gpointer user)
{
    emitBrowse(static_cast<BrowseState*>(user), "complete");
}

void onFailure(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*,
               GVariant*, gpointer user)
{
    // A browser that failed stops producing; the current set is still valid, so
    // the subscriber is left with what it had rather than an error after the
    // fact. A one-shot browse has already replied by AllForNow.
    (void)user;
}

GVariant* call(GDBusConnection* bus, const char* path, const char* iface, const char* method,
               GVariant* args, const GVariantType* reply, std::string& error)
{
    if (!bus) {
        error = "no system bus";
        if (args)
            g_variant_unref(g_variant_ref_sink(args));
        return nullptr;
    }
    GError* gerror = nullptr;
    GVariant* out = g_dbus_connection_call_sync(
        bus, kService, path, iface, method, args, reply,
        G_DBUS_CALL_FLAGS_NONE, kTimeoutMs, nullptr, &gerror);
    if (!out)
        error = errorFrom(gerror, "avahi did not answer");
    return out;
}

} // namespace

bool startBrowse(GDBusConnection* bus, const std::string& type,
                 BrowseCallback callback, Browser& browser, std::string& error)
{
    GVariant* reply = call(bus, kServerPath, kServerIface, "ServiceBrowserNew",
                           g_variant_new("(iissu)", kIfaceUnspec, kProtoUnspec,
                                         type.c_str(), "", kNoFlags),
                           G_VARIANT_TYPE("(o)"), error);
    if (!reply)
        return false;
    const char* path = nullptr;
    g_variant_get(reply, "(&o)", &path);
    browser.bus = bus;
    browser.path = path ? path : "";
    g_variant_unref(reply);
    if (browser.path.empty()) {
        error = "avahi returned no browser";
        return false;
    }

    // The state outlives this call; it is owned by the signal subscriptions and
    // freed in stopBrowse. new, not a smart pointer, because its lifetime is the
    // subscriptions', not this scope's.
    BrowseState* state = new BrowseState{ bus, type, std::move(callback), {} };

    const std::string path_s = browser.path;
    browser.itemNew = g_dbus_connection_signal_subscribe(
        bus, kService, kBrowserIface, "ItemNew", path_s.c_str(), nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, onItemNew, state, nullptr);
    browser.itemRemove = g_dbus_connection_signal_subscribe(
        bus, kService, kBrowserIface, "ItemRemove", path_s.c_str(), nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, onItemRemove, state, nullptr);
    browser.allForNow = g_dbus_connection_signal_subscribe(
        bus, kService, kBrowserIface, "AllForNow", path_s.c_str(), nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, onAllForNow, state,
        // The last subscription frees the shared state when all are gone.
        reinterpret_cast<GDestroyNotify>(+[](gpointer p) { delete static_cast<BrowseState*>(p); }));
    browser.failure = g_dbus_connection_signal_subscribe(
        bus, kService, kBrowserIface, "Failure", path_s.c_str(), nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, onFailure, state, nullptr);
    return true;
}

void stopBrowse(Browser& browser)
{
    if (!browser.bus)
        return;
    for (guint id : { browser.itemNew, browser.itemRemove, browser.failure, browser.allForNow })
        if (id)
            g_dbus_connection_signal_unsubscribe(browser.bus, id);
    if (!browser.path.empty()) {
        std::string ignored;
        GVariant* reply = call(browser.bus, browser.path.c_str(), kBrowserIface, "Free",
                               nullptr, nullptr, ignored);
        if (reply)
            g_variant_unref(reply);
    }
    browser = Browser{};
}

bool resolve(GDBusConnection* bus, const std::string& name, const std::string& type,
             const std::string& domain, Zeroconf::Resolved& resolved, std::string& error)
{
    GVariant* reply = call(
        bus, kServerPath, kServerIface, "ResolveService",
        g_variant_new("(iisssiu)", kIfaceUnspec, kProtoUnspec, name.c_str(),
                      type.c_str(), domain.empty() ? "local" : domain.c_str(),
                      kProtoUnspec, kNoFlags),
        G_VARIANT_TYPE("(iissssisqaayu)"), error);
    if (!reply)
        return false;

    gint32 iface = 0, proto = 0, aproto = 0;
    const gchar* rname = nullptr;
    const gchar* rtype = nullptr;
    const gchar* rdomain = nullptr;
    const gchar* host = nullptr;
    const gchar* address = nullptr;
    guint16 port = 0;
    GVariant* txt = nullptr;
    guint32 flags = 0;
    g_variant_get(reply, "(ii&s&s&s&si&sq@aayu)", &iface, &proto, &rname, &rtype,
                  &rdomain, &host, &aproto, &address, &port, &txt, &flags);

    resolved.name = rname ? rname : name;
    resolved.type = rtype ? rtype : type;
    resolved.domain = rdomain ? rdomain : domain;
    resolved.hostname = host ? host : "";
    resolved.address = address ? address : "";
    resolved.port = port;
    resolved.txt.clear();

    // TXT is aay: an array of byte strings, each "key=value" or a bare "key".
    GVariantIter iter;
    GVariant* entry = nullptr;
    g_variant_iter_init(&iter, txt);
    while ((entry = g_variant_iter_next_value(&iter))) {
        gsize length = 0;
        const guchar* bytes = static_cast<const guchar*>(
            g_variant_get_fixed_array(entry, &length, sizeof(guchar)));
        if (bytes && length) {
            const std::string raw(reinterpret_cast<const char*>(bytes), length);
            std::string key, value;
            if (Zeroconf::parseTxtEntry(raw, key, value))
                resolved.txt[key] = value;
        }
        g_variant_unref(entry);
    }
    g_variant_unref(txt);
    g_variant_unref(reply);
    return true;
}

bool registerService(GDBusConnection* bus, const Zeroconf::Service& service, int port,
                     const std::vector<std::pair<std::string, std::string>>& txt,
                     Entry& entry, std::string& error)
{
    GVariant* reply = call(bus, kServerPath, kServerIface, "EntryGroupNew",
                           nullptr, G_VARIANT_TYPE("(o)"), error);
    if (!reply)
        return false;
    const char* path = nullptr;
    g_variant_get(reply, "(&o)", &path);
    entry.bus = bus;
    entry.path = path ? path : "";
    g_variant_unref(reply);
    if (entry.path.empty()) {
        error = "avahi returned no entry group";
        return false;
    }

    // TXT back to Avahi's aay: each "key=value" (or bare "key") as a byte array.
    GVariantBuilder txtBuilder;
    g_variant_builder_init(&txtBuilder, G_VARIANT_TYPE("aay"));
    for (const auto& kv : txt) {
        std::string entryStr = kv.first;
        if (!kv.second.empty())
            entryStr += "=" + kv.second;
        GVariantBuilder bytes;
        g_variant_builder_init(&bytes, G_VARIANT_TYPE("ay"));
        for (unsigned char c : entryStr)
            g_variant_builder_add(&bytes, "y", c);
        g_variant_builder_add_value(&txtBuilder, g_variant_builder_end(&bytes));
    }

    GVariant* addReply = call(
        bus, entry.path.c_str(), kEntryGroupIface, "AddService",
        g_variant_new("(iiussssqaay)", kIfaceUnspec, kProtoUnspec, kNoFlags,
                      service.name.c_str(), service.type.c_str(),
                      service.domain.empty() ? "" : service.domain.c_str(),
                      "", static_cast<guint16>(port), &txtBuilder),
        nullptr, error);
    if (!addReply) {
        unregisterService(entry);
        return false;
    }
    g_variant_unref(addReply);

    GVariant* commit = call(bus, entry.path.c_str(), kEntryGroupIface, "Commit",
                            nullptr, nullptr, error);
    if (!commit) {
        unregisterService(entry);
        return false;
    }
    g_variant_unref(commit);
    return true;
}

void unregisterService(Entry& entry)
{
    if (entry.bus && !entry.path.empty()) {
        std::string ignored;
        GVariant* reply = call(entry.bus, entry.path.c_str(), kEntryGroupIface, "Free",
                               nullptr, nullptr, ignored);
        if (reply)
            g_variant_unref(reply);
    }
    entry = Entry{};
}

} // namespace AvahiClient
