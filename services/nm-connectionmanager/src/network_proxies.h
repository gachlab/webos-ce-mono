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
// Per-network proxy settings for com.palm.connectionmanager's
// getNwProxiesConfig / configureNwProxies (#23).
//
// HP's Networking card and enyo's lib/networkproxy speak this shape. The store
// is a JSON file ($WEBOS_NETWORK_PROXIES, or webos-ce/network-proxies.json in
// the user's data directory). Scope for wifi is the profileId as a string.
// WebAppMgr turns the entry for the active wifi profile into Qt's application
// proxy (see NetworkAppProxyAdapter); the pure pick lives here so tests need
// no Qt and no bus.
//

#ifndef NETWORK_PROXIES_H
#define NETWORK_PROXIES_H

#include "network_state.h"

#include <string>
#include <vector>

namespace NmNet {

// HP proxyConfigType values. autoDetectFromNetwork is accepted and may leave
// the store unchanged; noProxy is how a configure "add" clears an entry (rmv
// does the same).
inline constexpr const char* kProxyNone = "noProxy";
inline constexpr const char* kProxyManual = "manualProxy";
inline constexpr const char* kProxyPac = "autoConfigUrl";
inline constexpr const char* kProxyAutoDetect = "autoDetectFromNetwork";

struct ProxyInfo {
    std::string networkTechnology;
    std::string proxyScope;
    std::string proxyConfigType;
    std::string proxyServer;
    int proxyPort = 0;
    bool hasPort = false;
    std::string proxyAutoConfigUrl;
    bool isProxySecured = false;
    bool hasSecured = false;
};

inline bool sameProxyKey(const ProxyInfo& a, const ProxyInfo& b)
{
    return a.networkTechnology == b.networkTechnology && a.proxyScope == b.proxyScope;
}

inline std::string proxyInfoObject(const ProxyInfo& info)
{
    std::string out = "{\"networkTechnology\":\"" + jsonEscape(info.networkTechnology)
                      + "\",\"proxyScope\":\"" + jsonEscape(info.proxyScope)
                      + "\",\"proxyConfigType\":\"" + jsonEscape(info.proxyConfigType) + "\"";
    if (info.proxyConfigType == kProxyManual) {
        out += ",\"proxyServer\":\"" + jsonEscape(info.proxyServer) + "\"";
        if (info.hasPort)
            out += ",\"proxyPort\":" + std::to_string(info.proxyPort);
        if (info.hasSecured)
            out += info.isProxySecured ? ",\"isProxySecured\":true" : ",\"isProxySecured\":false";
    } else if (info.proxyConfigType == kProxyPac) {
        out += ",\"proxyAutoConfigUrl\":\"" + jsonEscape(info.proxyAutoConfigUrl) + "\"";
    }
    out += "}";
    return out;
}

inline std::string proxiesConfigPayload(const std::vector<ProxyInfo>& list, bool subscribed = false)
{
    std::string out = "{\"returnValue\":true,\"subscribed\":";
    out += subscribed ? "true" : "false";
    out += ",\"proxyInfoList\":[";
    for (size_t i = 0; i < list.size(); ++i) {
        if (i)
            out += ",";
        out += proxyInfoObject(list[i]);
    }
    out += "]}";
    return out;
}

// Apply configureNwProxies to an in-memory list. Empty string on success.
inline std::string configureProxies(std::vector<ProxyInfo>& list,
                                    const std::string& action,
                                    const ProxyInfo& info)
{
    if (info.networkTechnology.empty() || info.proxyScope.empty())
        return "networkTechnology and proxyScope required";
    if (action != "add" && action != "rmv")
        return "action must be \"add\" or \"rmv\"";

    if (action == "rmv" || info.proxyConfigType == kProxyNone) {
        std::vector<ProxyInfo> kept;
        kept.reserve(list.size());
        for (const ProxyInfo& entry : list) {
            if (!sameProxyKey(entry, info))
                kept.push_back(entry);
        }
        list.swap(kept);
        return {};
    }

    // HP can ask for WPAD without us inventing a PAC URL; accept and keep the
    // store as it is.
    if (info.proxyConfigType == kProxyAutoDetect)
        return {};

    if (info.proxyConfigType != kProxyManual && info.proxyConfigType != kProxyPac)
        return "unknown proxyConfigType";

    if (info.proxyConfigType == kProxyManual && info.proxyServer.empty())
        return "proxyServer required for manualProxy";
    if (info.proxyConfigType == kProxyPac && info.proxyAutoConfigUrl.empty())
        return "proxyAutoConfigUrl required for autoConfigUrl";

    for (ProxyInfo& entry : list) {
        if (sameProxyKey(entry, info)) {
            entry = info;
            return {};
        }
    }
    list.push_back(info);
    return {};
}

// The entry that applies while wifi profile `wifiProfileId` is up. nullptr
// when the radio is idle or that scope has no saved proxy.
inline const ProxyInfo* activeWifiProxy(const std::vector<ProxyInfo>& list, int wifiProfileId)
{
    if (wifiProfileId <= 0)
        return nullptr;
    const std::string scope = std::to_string(wifiProfileId);
    for (const ProxyInfo& entry : list) {
        if (entry.networkTechnology == "wifi" && entry.proxyScope == scope)
            return &entry;
    }
    return nullptr;
}

// What WebAppMgr hands Qt / Chromium. Pac is carried for the chromium flag
// path; QNetworkProxy itself only has host+port (manual).
struct AppProxy {
    enum class Kind { None, Manual, Pac } kind = Kind::None;
    std::string host;
    int port = 0;
    bool secure = false;
    std::string pacUrl;
};

inline bool operator==(const AppProxy& a, const AppProxy& b)
{
    return a.kind == b.kind && a.host == b.host && a.port == b.port && a.secure == b.secure
           && a.pacUrl == b.pacUrl;
}

inline AppProxy appProxyOf(const ProxyInfo* info)
{
    AppProxy out;
    if (!info)
        return out;
    if (info->proxyConfigType == kProxyManual && !info->proxyServer.empty()) {
        out.kind = AppProxy::Kind::Manual;
        out.host = info->proxyServer;
        // Optional in the card; HTTP proxies default to 8080 when omitted.
        out.port = info->hasPort ? info->proxyPort : 8080;
        out.secure = info->isProxySecured;
        return out;
    }
    if (info->proxyConfigType == kProxyPac && !info->proxyAutoConfigUrl.empty()) {
        out.kind = AppProxy::Kind::Pac;
        out.pacUrl = info->proxyAutoConfigUrl;
        return out;
    }
    return out;
}

inline AppProxy appProxyFor(const std::vector<ProxyInfo>& list, int wifiProfileId)
{
    return appProxyOf(activeWifiProxy(list, wifiProfileId));
}

} // namespace NmNet

namespace NetworkProxies {

// $WEBOS_NETWORK_PROXIES, or ~/.local/share/webos-ce/network-proxies.json.
std::string path();

std::vector<NmNet::ProxyInfo> load(const std::string& filePath);

// Writes {"proxyInfoList":[...]}. Creates parent directories as needed.
bool save(const std::string& filePath, const std::vector<NmNet::ProxyInfo>& list,
          std::string& error);

} // namespace NetworkProxies

#endif
