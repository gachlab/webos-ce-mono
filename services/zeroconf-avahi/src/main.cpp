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
// com.palm.zeroconf, answered from the host's Avahi (org.freedesktop.Avahi).
//
// HP's zeroconf shipped only on the device and was never released, and nothing
// in this tree invokes a method on it -- enyo carries only the URI alias. So
// unlike every other service here, the method and payload names are NOT a
// surviving HP contract: they are the DNS-SD vocabulary (browse, resolve,
// register) with this tree's subscription idiom, and they are PROVISIONAL. See
// zeroconf_records.h for the full note on where the API comes from and why it
// is allowed to change under the first real caller.
//
// zeroconf_records.h is the vocabulary and payloads, free of both buses, so
// tests/zeroconf-records.cpp checks them without Avahi and without ls-hubd.
// avahi_client.cpp is everything said to Avahi and takes the D-Bus connection
// as an argument, so tests/avahi-client.cpp runs it against a fake Avahi on a
// private bus. This file hands it the system bus and answers the webOS bus.
//
// Why C++: the same reason as nm-connectionmanager and services/bluetooth -- it
// needs a D-Bus client, and gio already provides one to anything linking glib.
//

#include "zeroconf_records.h"
#include "avahi_client.h"

#include <luna-service2/lunaservice.h>

#include <cjson/json.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib-unix.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

const char kServiceName[] = "com.palm.zeroconf";
const char kCategory[] = "/";

GMainLoop* g_loop = nullptr;
LSPalmService* g_service = nullptr;
GDBusConnection* g_system = nullptr;

void logAndFree(const char* where, LSError& error)
{
    g_warning("zeroconf-avahi: %s: %s", where, error.message ? error.message : "(no message)");
    LSErrorFree(&error);
}

void reply(LSHandle* sh, LSMessage* message, const std::string& payload)
{
    LSError error;
    LSErrorInit(&error);
    if (!LSMessageReply(sh, message, payload.c_str(), &error))
        logAndFree("LSMessageReply", error);
}

json_object* requestOf(LSMessage* message)
{
    const char* payload = LSMessageGetPayload(message);
    if (!payload)
        return nullptr;
    json_object* root = json_tokener_parse(payload);
    if (!root || is_error(root))
        return nullptr;
    if (!json_object_is_type(root, json_type_object)) {
        json_object_put(root);
        return nullptr;
    }
    return root;
}

std::string stringMember(json_object* object, const char* name)
{
    if (!object)
        return std::string();
    json_object* value = json_object_object_get(object, name);
    if (value && !is_error(value) && json_object_is_type(value, json_type_string))
        return json_object_get_string(value);
    return std::string();
}

int intMember(json_object* object, const char* name)
{
    if (!object)
        return 0;
    json_object* value = json_object_object_get(object, name);
    if (value && !is_error(value) && json_object_is_type(value, json_type_int))
        return json_object_get_int(value);
    return 0;
}

bool boolMember(json_object* object, const char* name)
{
    if (!object)
        return false;
    json_object* value = json_object_object_get(object, name);
    return value && !is_error(value) && json_object_is_type(value, json_type_boolean)
           && json_object_get_boolean(value);
}

// --- browse -----------------------------------------------------------------
//
// One live Avahi browser per service type, shared by every subscriber of that
// type: Avahi caches, so a second browser for the same type is wasted work.
// The browser is kept while any subscriber remains and torn down when the last
// goes; a one-shot browse borrows the same cache and replies on "complete".

struct TypeBrowse {
    AvahiClient::Browser browser;
    std::vector<Zeroconf::Service> services;
    std::string lastPayload;
};
std::map<std::string, std::unique_ptr<TypeBrowse>> g_browses;

const char kBrowseMethod[] = "browse";

void postBrowse(const std::string& type, const char* changed)
{
    auto it = g_browses.find(type);
    if (it == g_browses.end())
        return;
    const std::string payload = Zeroconf::browsePayload(it->second->services, true, changed);
    // Nothing changed for a subscriber to read -- the same instance seen on a
    // second interface -- is not posted.
    if (payload == it->second->lastPayload)
        return;
    it->second->lastPayload = payload;
    LSHandle* const handles[] = {
        LSPalmServiceGetPrivateConnection(g_service),
        LSPalmServiceGetPublicConnection(g_service),
    };
    for (LSHandle* handle : handles) {
        if (!handle)
            continue;
        LSError error;
        LSErrorInit(&error);
        if (!LSSubscriptionPost(handle, kCategory, kBrowseMethod, payload.c_str(), &error))
            logAndFree("LSSubscriptionPost", error);
    }
}

bool ensureBrowse(const std::string& type, std::string& error)
{
    if (g_browses.count(type))
        return true;
    auto entry = std::make_unique<TypeBrowse>();
    TypeBrowse* raw = entry.get();
    const std::string typeCopy = type;
    const bool ok = AvahiClient::startBrowse(
        g_system, type,
        [raw, typeCopy](const std::vector<Zeroconf::Service>& services, const char* changed) {
            raw->services = services;
            postBrowse(typeCopy, changed);
        },
        raw->browser, error);
    if (!ok)
        return false;
    g_browses[type] = std::move(entry);
    return true;
}

bool browse(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = requestOf(message);
    const std::string type = stringMember(root, "serviceType");
    if (root)
        json_object_put(root);

    if (!Zeroconf::validServiceType(type)) {
        reply(sh, message, Zeroconf::errorPayload("expected {\"serviceType\": \"_x._tcp\"}"));
        return true;
    }

    bool subscribed = false;
    LSError error;
    LSErrorInit(&error);
    if (!LSSubscriptionProcess(sh, message, &subscribed, &error))
        logAndFree("LSSubscriptionProcess", error);

    std::string problem;
    if (!ensureBrowse(type, problem)) {
        reply(sh, message, Zeroconf::errorPayload(problem));
        return true;
    }
    // The current set now, whether or not a subscription was asked for. Avahi's
    // cache answers a one-shot browse immediately; a subscriber then hears
    // changes through postBrowse.
    const std::vector<Zeroconf::Service>& services = g_browses[type]->services;
    reply(sh, message, Zeroconf::browsePayload(services, subscribed, nullptr));
    return true;
}

// --- resolve ----------------------------------------------------------------

bool resolve(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = requestOf(message);
    const std::string name = stringMember(root, "name");
    const std::string type = stringMember(root, "serviceType");
    const std::string domain = stringMember(root, "domain");
    if (root)
        json_object_put(root);

    if (!Zeroconf::validName(name) || !Zeroconf::validServiceType(type)) {
        reply(sh, message,
              Zeroconf::errorPayload("expected {\"name\": string, \"serviceType\": \"_x._tcp\"}"));
        return true;
    }
    Zeroconf::Resolved resolved;
    std::string error;
    if (!AvahiClient::resolve(g_system, name, type, domain, resolved, error)) {
        reply(sh, message, Zeroconf::errorPayload(error.empty() ? "could not resolve" : error));
        return true;
    }
    reply(sh, message, Zeroconf::resolvePayload(resolved));
    return true;
}

// --- register / unregister --------------------------------------------------
//
// A published service lives as long as the client that asked for it is
// subscribed: register declares the subscription, and the entry group is freed
// when the subscription drops (the client went, or called unregister). Avahi
// withdraws the service from the network the moment the group is freed, so a
// crashed publisher does not leave a stale record behind.

struct Published {
    AvahiClient::Entry entry;
    Zeroconf::Service service;
    int port = 0;
};
// Keyed by "type|name|port", the identity of a published service.
std::map<std::string, std::unique_ptr<Published>> g_published;

std::string publishedKey(const Zeroconf::Service& s, int port)
{
    return s.type + "|" + s.name + "|" + std::to_string(port);
}

bool registerService(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = requestOf(message);
    Zeroconf::Service service;
    service.name = stringMember(root, "name");
    service.type = stringMember(root, "serviceType");
    service.domain = stringMember(root, "domain");
    const int port = intMember(root, "port");
    std::vector<std::pair<std::string, std::string>> txt;
    if (root) {
        json_object* txtObj = json_object_object_get(root, "txt");
        if (txtObj && !is_error(txtObj) && json_object_is_type(txtObj, json_type_object)) {
            json_object_object_foreach(txtObj, key, val)
            {
                if (val && json_object_is_type(val, json_type_string))
                    txt.emplace_back(key, json_object_get_string(val));
            }
        }
        json_object_put(root);
    }

    if (!Zeroconf::validName(service.name) || !Zeroconf::validServiceType(service.type)
        || !Zeroconf::validPort(port)) {
        reply(sh, message, Zeroconf::errorPayload(
            "expected {\"name\": string, \"serviceType\": \"_x._tcp\", \"port\": 1..65535}"));
        return true;
    }

    const std::string key = publishedKey(service, port);
    if (!g_published.count(key)) {
        auto published = std::make_unique<Published>();
        published->service = service;
        published->port = port;
        std::string error;
        if (!AvahiClient::registerService(g_system, service, port, txt, published->entry, error)) {
            reply(sh, message, Zeroconf::errorPayload(error));
            return true;
        }
        g_published[key] = std::move(published);
    }

    // The subscription is what keeps the service alive; its cancel frees the
    // group. A register without subscribe:true publishes for this process's
    // lifetime (until unregister), which is the honest reading of "register".
    bool subscribed = false;
    LSError error;
    LSErrorInit(&error);
    if (!LSSubscriptionProcess(sh, message, &subscribed, &error))
        logAndFree("LSSubscriptionProcess", error);

    reply(sh, message, Zeroconf::registeredPayload(service));
    return true;
}

bool unregisterService(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = requestOf(message);
    Zeroconf::Service service;
    service.name = stringMember(root, "name");
    service.type = stringMember(root, "serviceType");
    const int port = intMember(root, "port");
    if (root)
        json_object_put(root);

    const std::string key = publishedKey(service, port);
    auto it = g_published.find(key);
    if (it != g_published.end()) {
        AvahiClient::unregisterService(it->second->entry);
        g_published.erase(it);
    }
    // Unregistering something not published is success: the end state the
    // caller asked for is reached either way.
    reply(sh, message, "{\"returnValue\":true}");
    return true;
}

LSMethod kMethods[] = {
    { "browse", browse },
    { "resolve", resolve },
    { "register", registerService },
    { "unregister", unregisterService },
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

    // The system bus, where Avahi lives. If it is not reachable the service
    // still starts and every call reports it, rather than the shell losing the
    // name altogether.
    GError* gerror = nullptr;
    g_system = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &gerror);
    if (!g_system) {
        g_warning("zeroconf-avahi: no system bus: %s",
                  gerror && gerror->message ? gerror->message : "(no message)");
        g_clear_error(&gerror);
    }

    LSError error;
    LSErrorInit(&error);
    if (!LSRegisterPalmService(kServiceName, &g_service, &error)) {
        logAndFree("LSRegisterPalmService", error);
        return 1;
    }

    LSErrorInit(&error);
    if (!LSPalmServiceRegisterCategory(g_service, kCategory, kMethods, kMethods, nullptr,
                                       nullptr, &error)) {
        logAndFree("LSPalmServiceRegisterCategory", error);
        return 1;
    }

    LSErrorInit(&error);
    if (!LSGmainAttachPalmService(g_service, g_loop, &error)) {
        logAndFree("LSGmainAttachPalmService", error);
        return 1;
    }

    g_unix_signal_add(SIGTERM, quit, nullptr);
    g_unix_signal_add(SIGINT, quit, nullptr);

    g_message("zeroconf-avahi: com.palm.zeroconf up%s",
              g_system ? "" : " (no system bus; Avahi unreachable)");
    g_main_loop_run(g_loop);

    for (auto& entry : g_browses)
        AvahiClient::stopBrowse(entry.second->browser);
    for (auto& entry : g_published)
        AvahiClient::unregisterService(entry.second->entry);

    LSErrorInit(&error);
    if (!LSUnregisterPalmService(g_service, &error))
        logAndFree("LSUnregisterPalmService", error);
    if (g_system)
        g_object_unref(g_system);
    g_main_loop_unref(g_loop);
    return 0;
}
