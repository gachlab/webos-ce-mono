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
// the LS2 signals that palmbus cannot emit (see services/sysfs-powerd).
//
// The mapping from NetworkManager's state to webOS's payload is in
// network_state.h, free of both buses, so tests/network-state.cpp can check it
// without a D-Bus daemon and without ls-hubd. What is read from NetworkManager
// and asked of it is in nm_client.cpp, which tests/nm-client.cpp runs against a
// fake NetworkManager on a private bus.
//

#include "network_state.h"
#include "certificates.h"
#include "network_proxies.h"
#include "nm_client.h"
#include "sleep_watch.h"

#include <luna-service2/lunaservice.h>

#include <cjson/json.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib-unix.h>

#include <fstream>
#include <memory>
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

// The third name: the certificates an enterprise network logs in with. Nothing
// else in the tree answers it, and the Wi-Fi card is its only caller.
const char kCertificateServiceName[] = "com.palm.certificatemanager";

// The fourth: VPN profiles for the system menu drawer and our VPN card (#21).
const char kVpnServiceName[] = "com.palm.vpn";
const char* const kVpnProfileListMethods[] = { "getProfileList", nullptr };
const char* const kVpnStatusMethods[] = { "getStatus", nullptr };
const char* const kProxyMethods[] = { "getNwProxiesConfig", nullptr };

// For the signal subscriptions only; the calls themselves are in nm_client.cpp.
const char kNmService[] = "org.freedesktop.NetworkManager";
const char kNmIface[] = "org.freedesktop.NetworkManager";

GMainLoop* g_loop = nullptr;
LSPalmService* g_service = nullptr;
LSPalmService* g_wifiService = nullptr;
LSPalmService* g_certificateService = nullptr;
LSPalmService* g_vpnService = nullptr;
GDBusConnection* g_system = nullptr;
NmNet::NetworkState g_state;
std::string g_lastPayload;
std::string g_lastWifiKey;
std::string g_lastVpnPayload;
guint g_refreshPending = 0;
// The network the last connect asked for; see NetworkState::attemptedSsid.
std::string g_attemptedSsid;
bool g_attemptedEnterprise = false;
// The network com.palm.wifi last reported as joined; see leftNetworkPayload.
std::string g_joinedSsid;

// When Device Sleeps; see NmNet::sleepRadioAction. Kept across restarts in the
// session's preferences, beside the system preferences database.
const char kWakeOnWifiFile[] = "/var/luna/preferences/com.palm.connectionmanager.wakeonwifi";
bool g_keepWifiOnWhileAsleep = true;
bool g_radioOffForSleep = false;
std::unique_ptr<SleepWatch> g_sleepWatch;

void logAndFree(const char* where, LSError& error)
{
    g_warning("nm-connectionmanager: %s: %s", where,
              error.message ? error.message : "(no message)");
    LSErrorFree(&error);
}

void reply(LSHandle* sh, LSMessage* message, const std::string& payload);

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

void postMethods(LSPalmService* service, const char* const* methods, const std::string& payload)
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
        for (const char* const* method = methods; *method; ++method) {
            LSError error;
            LSErrorInit(&error);
            if (!LSSubscriptionPost(handle, kCategory, *method, payload.c_str(), &error))
                logAndFree("LSSubscriptionPost", error);
        }
    }
}

std::string vpnListPayload(bool subscribed)
{
    std::vector<NmNet::VpnProfile> profiles;
    std::string error;
    if (!NmClient::listVpnProfiles(g_system, profiles, error))
        return NmNet::errorPayload(error);
    return NmNet::vpnProfileListPayload(profiles, subscribed);
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
    state.attemptedEnterprise = g_attemptedEnterprise;
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
    const std::string vpnPayload = vpnListPayload(true);
    const bool changed = payload != g_lastPayload;
    const bool wifiChanged = wifiKey != g_lastWifiKey;
    const bool vpnChanged = vpnPayload != g_lastVpnPayload;
    if (!changed && !wifiChanged && !vpnChanged)
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
    if (vpnChanged) {
        g_lastVpnPayload = vpnPayload;
        postMethods(g_vpnService, kVpnProfileListMethods, vpnPayload);
        postMethods(g_vpnService, kVpnStatusMethods,
                    NmNet::vpnStatusPayload(g_state.vpnActive, true));
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


// --- When Device Sleeps -----------------------------------------------------

void loadWakeOnWifi()
{
    std::ifstream in(kWakeOnWifiFile);
    std::string mode;
    if (in >> mode)
        NmNet::parseWakeOnWifiMode(mode, g_keepWifiOnWhileAsleep);
}

bool saveWakeOnWifi()
{
    std::ofstream out(kWakeOnWifiFile, std::ios::trunc);
    out << NmNet::wakeOnWifiMode(g_keepWifiOnWhileAsleep) << '\n';
    return static_cast<bool>(out);
}

void onPrepareForSleep(bool goingToSleep)
{
    const NmNet::NetworkState now = NmClient::readState(g_system);
    const NmNet::SleepRadio action = NmNet::sleepRadioAction(
        g_keepWifiOnWhileAsleep, goingToSleep, now.wifi.present && now.wifiEnabled, g_radioOffForSleep);
    std::string error;
    if (action == NmNet::SleepRadio::TurnOff) {
        g_message("nm-connectionmanager: going to sleep, switching wifi off");
        if (!NmClient::setWifiEnabled(g_system, false, error))
            g_warning("nm-connectionmanager: wifi stayed on: %s", error.c_str());
    } else if (action == NmNet::SleepRadio::TurnOn) {
        g_message("nm-connectionmanager: awake, switching wifi back on");
        if (!NmClient::setWifiEnabled(g_system, true, error))
            g_warning("nm-connectionmanager: wifi stayed off: %s", error.c_str());
    }
    if (!g_sleepWatch)
        return;
    // Released once the radio is dealt with, which is what lets the machine
    // sleep; taken again on waking, for the next time.
    if (goingToSleep)
        g_sleepWatch->hold(false);
    else
        g_sleepWatch->hold(!g_keepWifiOnWhileAsleep);
}

bool getWakeOnWifiMode(LSHandle* sh, LSMessage* message, void*)
{
    LSError error;
    LSErrorInit(&error);
    if (!LSMessageReply(sh, message, NmNet::wakeOnWifiPayload(g_keepWifiOnWhileAsleep).c_str(), &error))
        logAndFree("LSMessageReply", error);
    return true;
}

// --- proxies and connectivity (#23) -----------------------------------------

NmNet::ProxyInfo proxyInfoOf(json_object* root)
{
    NmNet::ProxyInfo info;
    if (!root)
        return info;
    json_object* nested = json_object_object_get(root, "proxyInfo");
    json_object* object = (nested && !is_error(nested) && json_object_is_type(nested, json_type_object))
                          ? nested : root;
    json_object* value = nullptr;
    value = json_object_object_get(object, "networkTechnology");
    if (value && !is_error(value) && json_object_is_type(value, json_type_string))
        info.networkTechnology = json_object_get_string(value);
    value = json_object_object_get(object, "proxyScope");
    if (value && !is_error(value)) {
        if (json_object_is_type(value, json_type_string))
            info.proxyScope = json_object_get_string(value);
        else if (json_object_is_type(value, json_type_int))
            info.proxyScope = std::to_string(json_object_get_int(value));
    }
    value = json_object_object_get(object, "proxyConfigType");
    if (value && !is_error(value) && json_object_is_type(value, json_type_string))
        info.proxyConfigType = json_object_get_string(value);
    value = json_object_object_get(object, "proxyServer");
    if (value && !is_error(value) && json_object_is_type(value, json_type_string))
        info.proxyServer = json_object_get_string(value);
    value = json_object_object_get(object, "proxyAutoConfigUrl");
    if (value && !is_error(value) && json_object_is_type(value, json_type_string))
        info.proxyAutoConfigUrl = json_object_get_string(value);
    value = json_object_object_get(object, "proxyPort");
    if (value && !is_error(value) && json_object_is_type(value, json_type_int)) {
        info.proxyPort = json_object_get_int(value);
        info.hasPort = true;
    }
    value = json_object_object_get(object, "isProxySecured");
    if (value && !is_error(value) && json_object_is_type(value, json_type_boolean)) {
        info.isProxySecured = json_object_get_boolean(value);
        info.hasSecured = true;
    }
    return info;
}

bool getNwProxiesConfig(LSHandle* sh, LSMessage* message, void*)
{
    bool subscribed = false;
    LSError error;
    LSErrorInit(&error);
    if (!LSSubscriptionProcess(sh, message, &subscribed, &error))
        logAndFree("LSSubscriptionProcess", error);

    reply(sh, message,
          NmNet::proxiesConfigPayload(NetworkProxies::load(NetworkProxies::path()), subscribed));
    return true;
}

bool configureNwProxies(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = nullptr;
    if (const char* payload = LSMessageGetPayload(message)) {
        root = json_tokener_parse(payload);
        if (root && is_error(root))
            root = nullptr;
    }
    std::string action;
    if (root) {
        json_object* value = json_object_object_get(root, "action");
        if (value && !is_error(value) && json_object_is_type(value, json_type_string))
            action = json_object_get_string(value);
    }
    const NmNet::ProxyInfo info = proxyInfoOf(root);
    if (root)
        json_object_put(root);

    std::vector<NmNet::ProxyInfo> list = NetworkProxies::load(NetworkProxies::path());
    const std::string problem = NmNet::configureProxies(list, action, info);
    if (!problem.empty()) {
        reply(sh, message, NmNet::errorPayload(problem));
        return true;
    }
    std::string error;
    if (!NetworkProxies::save(NetworkProxies::path(), list, error)) {
        reply(sh, message, NmNet::errorPayload(error));
        return true;
    }
    reply(sh, message, "{\"returnValue\":true}");
    // WebAppMgr (and anyone else) subscribed to getNwProxiesConfig so a save
    // reaches Qt's application proxy without waiting for a network change.
    postMethods(g_service, kProxyMethods, NmNet::proxiesConfigPayload(list, true));
    return true;
}

// Ours: answer from the last NetworkManager read kept in g_state. HP's card
// asked this after the captive login page; live updates still come from a
// getStatus subscription.
bool checkNetworkConnectivity(LSHandle* sh, LSMessage* message, void*)
{
    (void)message;
    const bool online = NmNet::internetAvailable(g_state);
    reply(sh, message,
          std::string("{\"returnValue\":true,\"isInternetConnectionAvailable\":")
              + (online ? "true" : "false") + "}");
    return true;
}

// {"mode": "enable" | "disable"}, answered with the mode now in force, which
// is what HP's card reads back to set its list.
bool setWakeOnWifiMode(LSHandle* sh, LSMessage* message, void*)
{
    std::string mode;
    if (const char* payload = LSMessageGetPayload(message)) {
        json_object* root = json_tokener_parse(payload);
        if (root && !is_error(root)) {
            json_object* value = json_object_object_get(root, "mode");
            if (value && !is_error(value) && json_object_is_type(value, json_type_string))
                mode = json_object_get_string(value);
            json_object_put(root);
        }
    }
    std::string reply;
    bool keepOn = g_keepWifiOnWhileAsleep;
    if (!NmNet::parseWakeOnWifiMode(mode, keepOn)) {
        reply = NmNet::errorPayload("expected {\"mode\": \"enable\" | \"disable\"}");
    } else {
        g_keepWifiOnWhileAsleep = keepOn;
        if (!saveWakeOnWifi())
            g_warning("nm-connectionmanager: could not save %s", kWakeOnWifiFile);
        if (g_sleepWatch)
            g_sleepWatch->hold(!g_keepWifiOnWhileAsleep);
        reply = NmNet::wakeOnWifiPayload(g_keepWifiOnWhileAsleep);
    }
    LSError error;
    LSErrorInit(&error);
    if (!LSMessageReply(sh, message, reply.c_str(), &error))
        logAndFree("LSMessageReply", error);
    return true;
}

LSMethod kMethods[] = {
    { "getStatus", getStatus },
    { "getstatus", getStatus },
    { "setWiredState", setWiredState },
    { "getWakeOnWiFiMode", getWakeOnWifiMode },
    { "setWakeOnWiFiMode", setWakeOnWifiMode },
    { "getNwProxiesConfig", getNwProxiesConfig },
    { "configureNwProxies", configureNwProxies },
    { "checkNetworkConnectivity", checkNetworkConnectivity },
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
        if (json_object* enterprise = member(security, "enterpriseSecurity", json_type_object)) {
            request.userId = stringMember(enterprise, "userId");
            request.password = stringMember(enterprise, "password");
            request.eapType = stringMember(enterprise, "eapType");
            request.clientCertificatePath = stringMember(enterprise, "clientCertificatePath");
            if (json_object* verify = member(enterprise, "verifyServerCert", json_type_boolean))
                request.verifyServerCert = json_object_get_boolean(verify);
        }
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
    // The address settings screen sends the profile back with useStaticIp set,
    // and the addresses under ipInfo when it is true.
    if (json_object* useStatic = member(root, "useStaticIp", json_type_boolean)) {
        request.addressChange = true;
        request.staticIp = json_object_get_boolean(useStatic);
        if (json_object* ip = member(root, "ipInfo", json_type_object)) {
            request.ip = stringMember(ip, "ip");
            request.subnet = stringMember(ip, "subnet");
            request.gateway = stringMember(ip, "gateway");
            request.dns1 = stringMember(ip, "dns1");
            request.dns2 = stringMember(ip, "dns2");
        }
    }
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
    const bool previousEnterprise = g_attemptedEnterprise;
    g_attemptedSsid = attempted;
    g_attemptedEnterprise = request.securityType == "enterprise";

    int profileId = 0;
    if (!NmClient::connectWifi(g_system, request, profileId, error)) {
        g_attemptedSsid = previous;
        g_attemptedEnterprise = previousEnterprise;
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

// com.palm.certificatemanager/listcertificates; see certificates.h.
bool listCertificates(LSHandle* sh, LSMessage* message, void*)
{
    reply(sh, message, NmNet::certificateListPayload(Certificates::list(Certificates::directory())));
    return true;
}

// --- com.palm.vpn (#21) -----------------------------------------------------

std::string jsonStringField(json_object* root, const char* key)
{
    if (!root)
        return std::string();
    json_object* value = json_object_object_get(root, key);
    if (!value || is_error(value) || !json_object_is_type(value, json_type_string))
        return std::string();
    return json_object_get_string(value);
}

json_object* parsePayload(LSMessage* message)
{
    const char* payload = LSMessageGetPayload(message);
    if (!payload)
        return nullptr;
    json_object* root = json_tokener_parse(payload);
    if (!root || is_error(root))
        return nullptr;
    return root;
}

NmNet::VpnRequest vpnRequestOf(json_object* root)
{
    NmNet::VpnRequest request;
    request.name = jsonStringField(root, "vpnProfileName");
    request.agentGuid = jsonStringField(root, "vpnAgentGuid");
    json_object* profile = json_object_object_get(root, "vpnProfile");
    if (profile && !is_error(profile) && json_object_is_type(profile, json_type_object)) {
        if (request.name.empty())
            request.name = jsonStringField(profile, "name");
        request.remote = jsonStringField(profile, "remote");
        request.userName = jsonStringField(profile, "userName");
        request.password = jsonStringField(profile, "password");
        request.privateKey = jsonStringField(profile, "privateKey");
        request.peerPublicKey = jsonStringField(profile, "peerPublicKey");
        request.address = jsonStringField(profile, "address");
        if (request.agentGuid.empty())
            request.agentGuid = jsonStringField(profile, "vpnAgentGuid");
    }
    // Flat fields the card may send without a nested vpnProfile.
    if (request.remote.empty())
        request.remote = jsonStringField(root, "remote");
    if (request.userName.empty())
        request.userName = jsonStringField(root, "userName");
    if (request.password.empty())
        request.password = jsonStringField(root, "password");
    if (request.privateKey.empty())
        request.privateKey = jsonStringField(root, "privateKey");
    if (request.peerPublicKey.empty())
        request.peerPublicKey = jsonStringField(root, "peerPublicKey");
    if (request.address.empty())
        request.address = jsonStringField(root, "address");
    return request;
}

bool getVpnProfileList(LSHandle* sh, LSMessage* message, void*)
{
    bool subscribed = false;
    LSError error;
    LSErrorInit(&error);
    if (!LSSubscriptionProcess(sh, message, &subscribed, &error))
        logAndFree("LSSubscriptionProcess", error);
    reply(sh, message, vpnListPayload(subscribed));
    return true;
}

bool getVpnStatus(LSHandle* sh, LSMessage* message, void*)
{
    bool subscribed = false;
    LSError error;
    LSErrorInit(&error);
    if (!LSSubscriptionProcess(sh, message, &subscribed, &error))
        logAndFree("LSSubscriptionProcess", error);
    reply(sh, message, NmNet::vpnStatusPayload(g_state.vpnActive, subscribed));
    return true;
}

bool getVpnAgents(LSHandle* sh, LSMessage* message, void*)
{
    reply(sh, message, NmNet::vpnAgentsPayload(NmNet::builtInVpnAgents()));
    return true;
}

bool getVpnProfileDetails(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = parsePayload(message);
    const std::string name = jsonStringField(root, "vpnProfileName");
    if (root)
        json_object_put(root);
    if (name.empty()) {
        reply(sh, message, NmNet::errorPayload("vpnProfileName required"));
        return true;
    }
    NmNet::VpnProfile profile;
    std::string error;
    if (!NmClient::getVpnProfile(g_system, name, profile, error)) {
        reply(sh, message, NmNet::errorPayload(error));
        return true;
    }
    reply(sh, message, NmNet::vpnProfileDetailsPayload(profile));
    return true;
}

bool getVpnConnectionDetails(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = parsePayload(message);
    const std::string name = jsonStringField(root, "vpnProfileName");
    if (root)
        json_object_put(root);
    if (name.empty()) {
        reply(sh, message, NmNet::errorPayload("vpnProfileName required"));
        return true;
    }
    NmNet::VpnProfile profile;
    std::string error;
    if (!NmClient::getVpnProfile(g_system, name, profile, error)) {
        reply(sh, message, NmNet::errorPayload(error));
        return true;
    }
    reply(sh, message, NmNet::vpnConnectionDetailsPayload(profile));
    return true;
}

bool addVpnProfile(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = parsePayload(message);
    const NmNet::VpnRequest request = vpnRequestOf(root);
    if (root)
        json_object_put(root);
    std::string error;
    if (!NmClient::addVpnProfile(g_system, request, error)) {
        reply(sh, message, NmNet::errorPayload(error));
        return true;
    }
    scheduleRefresh();
    reply(sh, message, "{\"returnValue\":true}");
    return true;
}

bool updateVpnProfile(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = parsePayload(message);
    const NmNet::VpnRequest request = vpnRequestOf(root);
    if (root)
        json_object_put(root);
    std::string error;
    if (!NmClient::updateVpnProfile(g_system, request, error)) {
        reply(sh, message, NmNet::errorPayload(error));
        return true;
    }
    scheduleRefresh();
    reply(sh, message, "{\"returnValue\":true}");
    return true;
}

bool deleteVpnProfile(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = parsePayload(message);
    const std::string name = jsonStringField(root, "vpnProfileName");
    if (root)
        json_object_put(root);
    std::string error;
    if (name.empty() || !NmClient::deleteVpnProfile(g_system, name, error)) {
        reply(sh, message, NmNet::errorPayload(name.empty() ? "vpnProfileName required" : error));
        return true;
    }
    scheduleRefresh();
    reply(sh, message, "{\"returnValue\":true}");
    return true;
}

bool connectVpn(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = parsePayload(message);
    // The system menu stringifies the whole list item and sends it back; the
    // card sends {vpnProfileName, vpnAgentGuid}.
    const std::string name = jsonStringField(root, "vpnProfileName");
    if (root)
        json_object_put(root);
    std::string error;
    if (name.empty() || !NmClient::connectVpn(g_system, name, error)) {
        reply(sh, message, NmNet::errorPayload(name.empty() ? "vpnProfileName required" : error));
        return true;
    }
    scheduleRefresh();
    reply(sh, message, "{\"returnValue\":true}");
    return true;
}

bool disconnectVpn(LSHandle* sh, LSMessage* message, void*)
{
    // The menu may pass the profile being switched to, not the one that is up.
    // Tear down whatever VPN is active.
    (void)message;
    std::string error;
    if (!NmClient::disconnectVpn(g_system, error)) {
        reply(sh, message, NmNet::errorPayload(error));
        return true;
    }
    scheduleRefresh();
    reply(sh, message, "{\"returnValue\":true}");
    return true;
}

LSMethod kCertificateMethods[] = {
    { "listcertificates", listCertificates },
    { },
};

LSMethod kVpnMethods[] = {
    { "getProfileList", getVpnProfileList },
    { "getStatus", getVpnStatus },
    { "getAgents", getVpnAgents },
    { "getProfileDetails", getVpnProfileDetails },
    { "getConnectionDetails", getVpnConnectionDetails },
    { "addProfile", addVpnProfile },
    { "updateProfile", updateVpnProfile },
    { "deleteProfile", deleteVpnProfile },
    { "connect", connectVpn },
    { "disconnect", disconnectVpn },
    { },
};

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

    // Not fatal either: without it, only the TLS login lacks its list.
    LSErrorInit(&error);
    if (!LSRegisterPalmService(kCertificateServiceName, &g_certificateService, &error)) {
        logAndFree("LSRegisterPalmService(com.palm.certificatemanager)", error);
        g_certificateService = nullptr;
    } else {
        LSErrorInit(&error);
        if (!LSPalmServiceRegisterCategory(g_certificateService, kCategory, kCertificateMethods,
                                           kCertificateMethods, nullptr, nullptr, &error)
            || !LSGmainAttachPalmService(g_certificateService, g_loop, &error)) {
            logAndFree("com.palm.certificatemanager", error);
            g_certificateService = nullptr;
        }
    }

    // The system menu drawer already calls this; without it the log says the
    // service does not exist and the drawer stays empty.
    LSErrorInit(&error);
    if (!LSRegisterPalmService(kVpnServiceName, &g_vpnService, &error)) {
        logAndFree("LSRegisterPalmService(com.palm.vpn)", error);
        g_vpnService = nullptr;
    } else {
        LSErrorInit(&error);
        if (!LSPalmServiceRegisterCategory(g_vpnService, kCategory, kVpnMethods,
                                           kVpnMethods, nullptr, nullptr, &error)
            || !LSGmainAttachPalmService(g_vpnService, g_loop, &error)) {
            logAndFree("com.palm.vpn", error);
            g_vpnService = nullptr;
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

    loadWakeOnWifi();
    g_sleepWatch = std::make_unique<SleepWatch>(g_system, onPrepareForSleep);
    g_sleepWatch->hold(!g_keepWifiOnWhileAsleep);

    // The state before anyone can ask for it, and g_lastPayload with it, so the
    // first real change is what gets posted rather than a duplicate of this.
    g_state = currentState();
    g_lastPayload = NmNet::statusPayload(g_state, true);
    g_lastWifiKey = NmNet::wifiChangeKey(g_state);
    g_lastVpnPayload = vpnListPayload(true);
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
    if (g_certificateService) {
        LSErrorInit(&error);
        if (!LSUnregisterPalmService(g_certificateService, &error))
            logAndFree("LSUnregisterPalmService(com.palm.certificatemanager)", error);
    }
    if (g_vpnService) {
        LSErrorInit(&error);
        if (!LSUnregisterPalmService(g_vpnService, &error))
            logAndFree("LSUnregisterPalmService(com.palm.vpn)", error);
    }
    g_sleepWatch.reset();
    if (g_system)
        g_object_unref(g_system);
    g_main_loop_unref(g_loop);
    return 0;
}
