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
// com.palm.keymanager, answered from the host's Secret Service.
//
// On a device this was a native service over an encrypted store the lock-screen
// passcode unlocked. Nothing in the CE drop provides it, so three callers have
// been talking to a service that is not there and failing quietly:
//
//   LunaSysMgr's Security.cpp   initialize {password}, changePassword
//                               {oldPassword,newPassword} -- fire-and-forget,
//                               and a registerServerStatus subscription that
//                               re-runs initialize whenever keymanager
//                               (re)appears. The name must register for real so
//                               that signal fires {"connected":true}.
//   the accounts service        fetchKey/store/remove/keyInfo {keyname,...} --
//                               the KeyStore the whole Synergy credential stack
//                               (put/get/del/has) sits on.
//
// The store is the host's Secret Service (org.freedesktop.secrets): one item per
// keyname, found by schema and keyname attribute. secret_client.cpp is
// everything said to it and takes the D-Bus connection as an argument, so
// tests/secret-client.cpp runs it against a fake Secret Service on a private
// bus; key_store.h is the mapping and the payloads, free of both buses, so
// tests/key-store.cpp checks every decision without a keyring and without
// ls-hubd. This file hands the client the session bus and answers the webOS bus.
//
// Why C++ and not JavaScript, which keymanager-adjacent code in the tree is: the
// store is reached over D-Bus, and gio already provides a D-Bus client to
// anything that links glib for its main loop. It is the same reasoning as
// nm-connectionmanager, and the same two files split the bus-free decision from
// the D-Bus talk for the same reason.
//

#include "key_store.h"
#include "secret_client.h"

#include <luna-service2/lunaservice.h>

#include <cjson/json.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib-unix.h>

#include <string>

namespace {

const char kServiceName[] = "com.palm.keymanager";
const char kCategory[] = "/";

GMainLoop* g_loop = nullptr;
LSPalmService* g_service = nullptr;
GDBusConnection* g_session = nullptr;

void logAndFree(const char* where, LSError& error)
{
    g_warning("keymanager-secret: %s: %s", where, error.message ? error.message : "(no message)");
    LSErrorFree(&error);
}

void reply(LSHandle* sh, LSMessage* message, const std::string& payload)
{
    LSError error;
    LSErrorInit(&error);
    if (!LSMessageReply(sh, message, payload.c_str(), &error))
        logAndFree("LSMessageReply", error);
}

// The request's JSON object, or nullptr; the caller frees it.
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

bool boolMember(json_object* object, const char* name)
{
    if (!object)
        return false;
    json_object* value = json_object_object_get(object, name);
    return value && !is_error(value) && json_object_is_type(value, json_type_boolean)
           && json_object_get_boolean(value);
}

// --- the bus methods --------------------------------------------------------

// Fire-and-forget from Security.cpp, but still answered: a registerServerStatus
// subscriber that never saw a reply would be none the wiser, while the accounts
// service never calls this at all. Reaching the store and settling the retry
// loop is the whole job.
bool initialize(LSHandle* sh, LSMessage* message, void*)
{
    std::string error;
    if (SecretClient::initialize(g_session, error))
        reply(sh, message, KeyStore::okPayload());
    else
        reply(sh, message, KeyStore::errorPayload(error));
    return true;
}

bool changePassword(LSHandle* sh, LSMessage* message, void*)
{
    std::string error;
    if (SecretClient::changePassword(g_session, error))
        reply(sh, message, KeyStore::okPayload());
    else
        reply(sh, message, KeyStore::errorPayload(error));
    return true;
}

bool store(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = requestOf(message);
    KeyStore::StoreRequest request;
    request.keyname = stringMember(root, "keyname");
    request.keydata = stringMember(root, "keydata");
    request.type = stringMember(root, "type");
    request.nohide = boolMember(root, "nohide");
    if (root)
        json_object_put(root);

    if (!KeyStore::validStore(request)) {
        reply(sh, message, KeyStore::errorPayload("expected {\"keyname\": string, \"keydata\": string}"));
        return true;
    }
    std::string error;
    if (SecretClient::store(g_session, request, error))
        reply(sh, message, KeyStore::okPayload());
    else
        reply(sh, message, KeyStore::errorPayload(error));
    return true;
}

bool fetchKey(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = requestOf(message);
    const std::string keyname = stringMember(root, "keyname");
    if (root)
        json_object_put(root);

    if (!KeyStore::validKeyname(keyname)) {
        reply(sh, message, KeyStore::errorPayload("expected {\"keyname\": string}"));
        return true;
    }
    std::string keydata, error;
    bool found = false;
    if (SecretClient::fetch(g_session, keyname, keydata, found, error) && found)
        reply(sh, message, KeyStore::fetchPayload(keydata));
    else
        reply(sh, message, KeyStore::errorPayload(error.empty() ? KeyStore::notFoundMessage() : error));
    return true;
}

bool remove(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = requestOf(message);
    const std::string keyname = stringMember(root, "keyname");
    if (root)
        json_object_put(root);

    if (!KeyStore::validKeyname(keyname)) {
        reply(sh, message, KeyStore::errorPayload("expected {\"keyname\": string}"));
        return true;
    }
    std::string error;
    if (SecretClient::remove(g_session, keyname, error))
        reply(sh, message, KeyStore::okPayload());
    else
        reply(sh, message, KeyStore::errorPayload(error));
    return true;
}

// Exists -> success; absent -> a failed reply, which the accounts service's
// hasCredentials turns into has()=false. A store that cannot be reached at all
// is a failure with the service's own message, told apart from a clean absence
// by secret_client.cpp setting error only for the former.
bool keyInfo(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = requestOf(message);
    const std::string keyname = stringMember(root, "keyname");
    if (root)
        json_object_put(root);

    if (!KeyStore::validKeyname(keyname)) {
        reply(sh, message, KeyStore::errorPayload("expected {\"keyname\": string}"));
        return true;
    }
    std::string error;
    bool present = false;
    if (!SecretClient::exists(g_session, keyname, present, error))
        reply(sh, message, KeyStore::errorPayload(error));
    else if (present)
        reply(sh, message, KeyStore::keyInfoPayload());
    else
        reply(sh, message, KeyStore::errorPayload(KeyStore::notFoundMessage()));
    return true;
}

LSMethod kMethods[] = {
    { "initialize", initialize },
    { "changePassword", changePassword },
    { "store", store },
    { "fetchKey", fetchKey },
    { "remove", remove },
    { "keyInfo", keyInfo },
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

    GError* gerror = nullptr;
    // The Secret Service lives on the session bus, not the system bus:
    // gnome-keyring and its kin are per-login-session daemons. A session bus
    // that is not there (a stripped service manager) leaves g_session null, and
    // every call reports "no session bus" rather than crashing -- which is the
    // honest state of a store that cannot be reached.
    g_session = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &gerror);
    if (!g_session) {
        g_warning("keymanager-secret: no session bus: %s",
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
    if (!LSPalmServiceRegisterCategory(g_service, kCategory, kMethods, nullptr, nullptr,
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

    g_message("keymanager-secret: com.palm.keymanager up%s",
              g_session ? "" : " (no session bus; store unreachable)");
    g_main_loop_run(g_loop);

    LSErrorInit(&error);
    if (!LSUnregisterPalmService(g_service, &error))
        logAndFree("LSUnregisterPalmService", error);
    if (g_session)
        g_object_unref(g_session);
    g_main_loop_unref(g_loop);
    return 0;
}
