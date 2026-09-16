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
// com.palm.connectionmanager, answered from NetworkManager.
//
// HP's own answer to this call is components/pmnetconfigmanager-stub, a
// JavaScript service that replies with a constant: connected, over wifi, on
// "Open webOS", 192.168.0.0, always. Every consumer in the tree believes it, so
// pulling the cable or switching wifi off changed nothing and the email app kept
// trying to sync against a network that was not there.
//
// Who is listening, measured rather than assumed -- all four subscribe with
// {"subscribe":true} and act on what arrives:
//
//   StatusBarServicesConnector.cpp     the wifi indicator
//   luna-sysservice's NetworkConnectionListener
//                                      fires connectionStateChanged, which is
//                                      what tells the rest of the system it is
//                                      offline
//   BrowserServer.cpp                  isInternetConnectionAvailable
//   activitymanager's ConnectionManagerProxy
//                                      the wifi/wan/*Confidence requirements
//                                      activities are scheduled against
//
// Why C++ and not JavaScript, which is what the stub is: this needs a D-Bus
// client, and gio already provides one to anything that links glib -- which
// every service here does for its main loop. The JavaScript route would have
// meant bundling a D-Bus library and building the npm machinery to vendor it,
// before writing a line of network logic. Subscriptions were NOT the reason:
// they were measured to work from a mojoservice JS service in this port, unlike
// the LS2 signals that palmbus cannot emit (see components/sysfs-powerd).
//
// The mapping from NetworkManager's state to webOS's payload is in
// network_state.h, free of both buses, so tests/network-state.cpp can check it
// without a D-Bus daemon and without ls-hubd. What is read from NetworkManager
// and asked of it is in nm_client.cpp, which tests/nm-client.cpp runs against a
// fake NetworkManager on a private bus.
//

#include "network_state.h"
#include "nm_client.h"

#include <luna-service2/lunaservice.h>

#include <cjson/json.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib-unix.h>

#include <string>
#include <vector>

namespace {

const char kServiceName[] = "com.palm.connectionmanager";
const char kCategory[] = "/";

// Both spellings, because HP's own callers disagree: activitymanager calls
// getStatus, while the status bar, luna-sysservice and BrowserServer call
// getstatus. The stub declared both in its services.json and so does this.
const char* const kStatusMethods[] = { "getStatus", "getstatus", nullptr };

// com.palm.wifi, the second name this process owns. It is the same network read
// from the same NetworkManager state, in the shape the status bar's wifi
// indicator wants -- see network_state.h. Registering both names in one process
// keeps them from ever disagreeing about what the radio is doing.
const char kWifiServiceName[] = "com.palm.wifi";

// For the signal subscriptions only; the calls themselves are in nm_client.cpp.
const char kNmService[] = "org.freedesktop.NetworkManager";
const char kNmIface[] = "org.freedesktop.NetworkManager";

GMainLoop* g_loop = nullptr;
LSPalmService* g_service = nullptr;
LSPalmService* g_wifiService = nullptr;
GDBusConnection* g_system = nullptr;
NmNet::NetworkState g_state;
std::string g_lastPayload;
std::string g_lastWifiKey;
guint g_refreshPending = 0;
// The network the last connect asked for; see NetworkState::attemptedSsid.
std::string g_attemptedSsid;
// The network com.palm.wifi last reported as joined; see leftNetworkPayload.
std::string g_joinedSsid;

void logAndFree(const char* where, LSError& error)
{
    g_warning("nm-connectionmanager: %s: %s", where,
              error.message ? error.message : "(no message)");
    LSErrorFree(&error);
}

// --- telling webOS ----------------------------------------------------------

void post(LSPalmService* service, const std::string& payload)
{
    if (!service)
        return;
    LSHandle* const handles[] = {
        LSPalmServiceGetPrivateConnection(service),
        LSPalmServiceGetPublicConnection(service),
    };
    for (LSHandle* handle : handles) {
        if (!handle)
            continue;
        for (const char* const* method = kStatusMethods; *method; ++method) {
            LSError error;
            LSErrorInit(&error);
            if (!LSSubscriptionPost(handle, kCategory, *method, payload.c_str(), &error))
                logAndFree("LSSubscriptionPost", error);
        }
    }
}

// The state, with what only this process knows added to what NM says.
NmNet::NetworkState currentState()
{
    NmNet::NetworkState state = NmClient::readState(g_system);
    // A join is over once the device is up; a failure stays reported until the
    // next join, which is what lets the settings app show why.
    if (state.wifi.activated())
        g_attemptedSsid.clear();
    state.attemptedSsid = g_attemptedSsid;
    return state;
}

void refresh()
{
    g_state = currentState();
    // Compared as the payload rather than field by field: if what the
    // subscribers would read has not changed, there is nothing to tell them.
    // NetworkManager emits PropertiesChanged for things webOS has no notion of
    // -- a container's veth appearing, a route metric moving -- and each one
    // would otherwise wake every consumer.
    // The two names are posted independently: the wifi indicator follows states
    // the connectionmanager payload does not distinguish -- joining a network
    // moves through associating and associated while both of its answers still
    // read "disconnected" -- so a shared guard would swallow those updates.
    const std::string payload = NmNet::statusPayload(g_state, true);
    const std::string wifiPayload = NmNet::wifiStatusPayload(g_state, true);
    const std::string wifiKey = NmNet::wifiChangeKey(g_state);
    const bool changed = payload != g_lastPayload;
    const bool wifiChanged = wifiKey != g_lastWifiKey;
    if (!changed && !wifiChanged)
        return;

    if (changed) {
        g_lastPayload = payload;
        g_message("nm-connectionmanager: wifi=%s%s%s wired=%s internet=%s",
                  NmNet::deviceState(g_state.wifi),
                  g_state.wifi.ssid.empty() ? "" : " ",
                  g_state.wifi.ssid.c_str(),
                  NmNet::deviceState(g_state.wired),
                  NmNet::internetAvailable(g_state) ? "yes" : "no");
        post(g_service, payload);
    }
    if (wifiChanged) {
        g_lastWifiKey = wifiKey;
        const std::string left = NmNet::leftNetworkPayload(g_joinedSsid, g_state);
        if (!left.empty())
            post(g_wifiService, left);
        g_joinedSsid = NmNet::joinedSsid(g_state);
        post(g_wifiService, wifiPayload);
    }
}

gboolean refreshNow(gpointer)
{
    g_refreshPending = 0;
    refresh();
    return G_SOURCE_REMOVE;
}

// Changes arrive in bursts: joining a network moves the device through five
// states and rewrites its address, each as its own signal. One read per burst.
void scheduleRefresh()
{
    if (!g_refreshPending)
        g_refreshPending = g_idle_add(refreshNow, nullptr);
}

void onNmSignal(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                const gchar*, GVariant*, gpointer)
{
    scheduleRefresh();
}

// --- the bus methods --------------------------------------------------------

bool getStatus(LSHandle* sh, LSMessage* message, void*)
{
    bool subscribed = false;
    LSError error;
    LSErrorInit(&error);
    if (!LSSubscriptionProcess(sh, message, &subscribed, &error))
        logAndFree("LSSubscriptionProcess", error);

    const std::string payload = NmNet::statusPayload(g_state, subscribed);
    LSErrorInit(&error);
    if (!LSMessageReply(sh, message, payload.c_str(), &error))
        logAndFree("LSMessageReply", error);
    return true;
}

// Ours, not HP's: his connectionmanager had no such method because a phone had
// no socket to unplug. The name is on com.palm.connectionmanager because that is
// where the wired state already lives.
bool setWiredState(LSHandle* sh, LSMessage* message, void*)
{
    bool wanted = false;
    bool parsed = false;
    const char* payload = LSMessageGetPayload(message);
    if (payload) {
        json_object* root = json_tokener_parse(payload);
        if (root && !is_error(root)) {
            json_object* label = json_object_object_get(root, "connected");
            if (label && json_object_is_type(label, json_type_boolean)) {
                wanted = json_object_get_boolean(label);
                parsed = true;
            }
            json_object_put(root);
        }
    }

    std::string reply;
    if (!parsed) {
        reply = "{\"returnValue\":false,\"errorText\":\"expected {\\\"connected\\\": boolean}\"}";
    } else {
        std::string error;
        if (NmClient::setWired(g_system, wanted, error)) {
            reply = "{\"returnValue\":true}";
            // NetworkManager's own signals will carry the new state to every
            // subscriber; this only shortens the wait for the first change.
            scheduleRefresh();
        } else {
            reply = std::string("{\"returnValue\":false,\"errorText\":\"")
                    + NmNet::jsonEscape(error) + "\"}";
        }
    }

    LSError lserror;
    LSErrorInit(&lserror);
    if (!LSMessageReply(sh, message, reply.c_str(), &lserror))
        logAndFree("LSMessageReply", lserror);
    return true;
}

LSMethod kMethods[] = {
    { "getStatus", getStatus },
    { "getstatus", getStatus },
    { "setWiredState", setWiredState },
    { },
};

// The status bar calls getstatus; enyo's wifi library calls it too. Both
// spellings again, for the same reason as above.
bool getWifiStatus(LSHandle* sh, LSMessage* message, void*)
{
    bool subscribed = false;
    LSError error;
    LSErrorInit(&error);
    if (!LSSubscriptionProcess(sh, message, &subscribed, &error))
        logAndFree("LSSubscriptionProcess", error);

    const std::string payload = NmNet::wifiStatusPayload(g_state, subscribed);
    LSErrorInit(&error);
    if (!LSMessageReply(sh, message, payload.c_str(), &error))
        logAndFree("LSMessageReply", error);
    return true;
}


// --- com.palm.wifi, phase 3 -------------------------------------------------

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
    if (!root || is_error(root) || !json_object_is_type(root, json_type_object)) {
        if (root && !is_error(root))
            json_object_put(root);
        return nullptr;
    }
    return root;
}

json_object* member(json_object* object, const char* name, json_type type)
{
    if (!object)
        return nullptr;
    json_object* value = json_object_object_get(object, name);
    return (value && !is_error(value) && json_object_is_type(value, type)) ? value : nullptr;
}

std::string stringMember(json_object* object, const char* name)
{
    json_object* value = member(object, name, json_type_string);
    return value ? json_object_get_string(value) : std::string();
}

// {"state": "enabled" | "disabled"}, from the menu's switch and the library.
bool setWifiState(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = requestOf(message);
    const std::string state = stringMember(root, "state");
    if (root)
        json_object_put(root);
    if (state != "enabled" && state != "disabled") {
        reply(sh, message, NmNet::errorPayload("expected {\"state\": \"enabled\" | \"disabled\"}"));
        return true;
    }
    std::string error;
    if (!NmClient::setWifiEnabled(g_system, state == "enabled", error)) {
        reply(sh, message, NmNet::errorPayload(error));
        return true;
    }
    reply(sh, message, "{\"returnValue\":true}");
    scheduleRefresh();
    return true;
}

bool findNetworks(LSHandle* sh, LSMessage* message, void*)
{
    std::vector<NmNet::AccessPoint> networks;
    std::string error;
    if (!NmClient::scan(g_system, networks, error)) {
        reply(sh, message, NmNet::errorPayload(error));
        return true;
    }
    reply(sh, message, NmNet::foundNetworksPayload(networks, g_state));
    return true;
}

// Two shapes arrive. The system menu sends {"profileId": n}, or {"ssid": s}
// with a top-level "securityType"; enyo's library sends the security inside
// "security", with the key under "simpleSecurity".
bool connectWifi(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = requestOf(message);
    if (!root) {
        reply(sh, message, NmNet::errorPayload("expected a JSON object"));
        return true;
    }
    NmNet::ConnectRequest request;
    if (json_object* id = member(root, "profileId", json_type_int))
        request.profileId = json_object_get_int(id);
    request.ssid = stringMember(root, "ssid");
    request.securityType = stringMember(root, "securityType");
    if (json_object* security = member(root, "security", json_type_object)) {
        const std::string type = stringMember(security, "securityType");
        if (!type.empty())
            request.securityType = type;
        if (json_object* simple = member(security, "simpleSecurity", json_type_object)) {
            request.passKey = stringMember(simple, "passKey");
            if (json_object* index = member(simple, "keyIndex", json_type_int))
                request.keyIndex = json_object_get_int(index);
            if (json_object* hex = member(simple, "isInHex", json_type_boolean))
                request.isInHex = json_object_get_boolean(hex);
        }
    }
    if (json_object* hidden = member(root, "wasCreatedWithJoinOther", json_type_boolean))
        request.hidden = json_object_get_boolean(hidden);
    json_object* useStatic = member(root, "useStaticIp", json_type_boolean);
    request.staticIp = (useStatic && json_object_get_boolean(useStatic))
                       || member(root, "ipInfo", json_type_object);
    json_object_put(root);

    std::string error = NmNet::validateConnect(request);
    if (!error.empty()) {
        reply(sh, message, NmNet::errorPayload(error));
        return true;
    }

    // Named before the call, so a failure reported by NM's signals while the
    // call is still returning already carries it.
    std::string attempted = request.ssid;
    if (attempted.empty()) {
        NmNet::Profile profile;
        NmNet::IpInfo ip;
        bool active = false;
        std::string ignored;
        if (NmClient::getProfile(g_system, request.profileId, profile, ip, active, ignored))
            attempted = profile.ssid;
    }
    const std::string previous = g_attemptedSsid;
    g_attemptedSsid = attempted;

    int profileId = 0;
    if (!NmClient::connectWifi(g_system, request, profileId, error)) {
        g_attemptedSsid = previous;
        reply(sh, message, NmNet::errorPayload(error));
        return true;
    }
    reply(sh, message, "{\"returnValue\":true,\"profileId\":" + std::to_string(profileId) + "}");
    scheduleRefresh();
    return true;
}

int profileIdOf(LSMessage* message)
{
    json_object* root = requestOf(message);
    json_object* id = member(root, "profileId", json_type_int);
    const int out = id ? json_object_get_int(id) : 0;
    if (root)
        json_object_put(root);
    return out;
}

bool getWifiProfile(LSHandle* sh, LSMessage* message, void*)
{
    const int id = profileIdOf(message);
    if (id <= 0) {
        reply(sh, message, NmNet::errorPayload("expected {\"profileId\": number}"));
        return true;
    }
    NmNet::Profile profile;
    NmNet::IpInfo ip;
    bool active = false;
    std::string error;
    if (!NmClient::getProfile(g_system, id, profile, ip, active, error)) {
        reply(sh, message, NmNet::errorPayload(error));
        return true;
    }
    reply(sh, message, NmNet::profilePayload(profile, active ? &ip : nullptr));
    return true;
}

// A profileId is required. enyo's library also calls this with no arguments,
// which on the phone meant "every saved network"; here that would delete the
// user's NetworkManager profiles, so it is refused.
bool deleteWifiProfile(LSHandle* sh, LSMessage* message, void*)
{
    const int id = profileIdOf(message);
    if (id <= 0) {
        reply(sh, message, NmNet::errorPayload("expected {\"profileId\": number}"));
        return true;
    }
    std::string error;
    if (!NmClient::deleteProfile(g_system, id, error)) {
        reply(sh, message, NmNet::errorPayload(error));
        return true;
    }
    reply(sh, message, "{\"returnValue\":true}");
    scheduleRefresh();
    return true;
}

bool getWifiProfileList(LSHandle* sh, LSMessage* message, void*)
{
    std::vector<NmNet::Profile> profiles;
    std::string error;
    if (!NmClient::listProfiles(g_system, profiles, error)) {
        reply(sh, message, NmNet::errorPayload(error));
        return true;
    }
    reply(sh, message, NmNet::profileListPayload(profiles));
    return true;
}

bool getWifiInfo(LSHandle* sh, LSMessage* message, void*)
{
    std::string mac, error;
    if (!NmClient::wifiMacAddress(g_system, mac, error)) {
        reply(sh, message, NmNet::errorPayload(error));
        return true;
    }
    reply(sh, message, NmNet::infoPayload(mac));
    return true;
}

LSMethod kWifiMethods[] = {
    { "getStatus", getWifiStatus },
    { "getstatus", getWifiStatus },
    { "setstate", setWifiState },
    { "findnetworks", findNetworks },
    { "connect", connectWifi },
    { "getprofile", getWifiProfile },
    { "deleteprofile", deleteWifiProfile },
    { "getprofilelist", getWifiProfileList },
    { "getinfo", getWifiInfo },
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

    // The system bus, where NetworkManager lives. Reachable from inside the
    // session's namespace: tools/run-lunasysmgr.sh binds the host's /run, and
    // org.freedesktop.NetworkManager answers there exactly as it does outside.
    // If it is not reachable the service still starts and reports a
    // disconnected machine, which is better than the shell losing
    // com.palm.connectionmanager altogether -- that is what the stub was added
    // to prevent in the first place.
    GError* gerror = nullptr;
    g_system = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &gerror);
    if (!g_system) {
        g_warning("nm-connectionmanager: no system bus: %s",
                  gerror ? gerror->message : "(no message)");
        g_clear_error(&gerror);
    }

    LSError error;
    LSErrorInit(&error);
    if (!LSRegisterPalmService(kServiceName, &g_service, &error)) {
        logAndFree("LSRegisterPalmService", error);
        return 1;
    }

    // On both buses, unlike sysfs-powerd: the callers are split across them --
    // the status bar and BrowserServer on one, luna-sysservice and
    // activitymanager on the other -- and HP's stub had a role on each.
    LSErrorInit(&error);
    if (!LSPalmServiceRegisterCategory(g_service, kCategory, kMethods, kMethods,
                                       nullptr, nullptr, &error)) {
        logAndFree("LSPalmServiceRegisterCategory", error);
        return 1;
    }

    LSErrorInit(&error);
    if (!LSGmainAttachPalmService(g_service, g_loop, &error)) {
        logAndFree("LSGmainAttachPalmService", error);
        return 1;
    }

    // com.palm.wifi. Not fatal if it cannot be had: com.palm.connectionmanager
    // is what the apps need to stop believing they are online, and losing the
    // indicator is better than losing both.
    LSErrorInit(&error);
    if (!LSRegisterPalmService(kWifiServiceName, &g_wifiService, &error)) {
        logAndFree("LSRegisterPalmService(com.palm.wifi)", error);
        g_wifiService = nullptr;
    } else {
        LSErrorInit(&error);
        if (!LSPalmServiceRegisterCategory(g_wifiService, kCategory, kWifiMethods,
                                           kWifiMethods, nullptr, nullptr, &error)) {
            logAndFree("LSPalmServiceRegisterCategory(com.palm.wifi)", error);
            g_wifiService = nullptr;
        } else {
            LSErrorInit(&error);
            if (!LSGmainAttachPalmService(g_wifiService, g_loop, &error)) {
                logAndFree("LSGmainAttachPalmService(com.palm.wifi)", error);
                g_wifiService = nullptr;
            }
        }
    }

    if (g_system) {
        // Everything NetworkManager says about itself and its objects. The
        // property signal carries the interface it belongs to, but filtering on
        // it here would mean listing every one that matters -- device, access
        // point, IP config, active connection -- and missing one shows up as a
        // state that stops updating in a specific case. Reading once per burst
        // is cheap enough to not need that risk.
        g_dbus_connection_signal_subscribe(
            g_system, kNmService, "org.freedesktop.DBus.Properties",
            "PropertiesChanged", nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
            onNmSignal, nullptr, nullptr);
        for (const char* member : { "DeviceAdded", "DeviceRemoved", "StateChanged" }) {
            g_dbus_connection_signal_subscribe(
                g_system, kNmService, kNmIface, member, nullptr, nullptr,
                G_DBUS_SIGNAL_FLAGS_NONE, onNmSignal, nullptr, nullptr);
        }
    }

    // The state before anyone can ask for it, and g_lastPayload with it, so the
    // first real change is what gets posted rather than a duplicate of this.
    g_state = currentState();
    g_lastPayload = NmNet::statusPayload(g_state, true);
    g_lastWifiKey = NmNet::wifiChangeKey(g_state);
    g_joinedSsid = NmNet::joinedSsid(g_state);
    g_message("nm-connectionmanager: com.palm.connectionmanager up, wifi=%s wired=%s internet=%s",
              NmNet::deviceState(g_state.wifi), NmNet::deviceState(g_state.wired),
              NmNet::internetAvailable(g_state) ? "yes" : "no");

    g_unix_signal_add(SIGTERM, quit, nullptr);
    g_unix_signal_add(SIGINT, quit, nullptr);

    g_main_loop_run(g_loop);

    LSErrorInit(&error);
    if (!LSUnregisterPalmService(g_service, &error))
        logAndFree("LSUnregisterPalmService", error);
    if (g_wifiService) {
        LSErrorInit(&error);
        if (!LSUnregisterPalmService(g_wifiService, &error))
            logAndFree("LSUnregisterPalmService(com.palm.wifi)", error);
    }
    if (g_system)
        g_object_unref(g_system);
    g_main_loop_unref(g_loop);
    return 0;
}
