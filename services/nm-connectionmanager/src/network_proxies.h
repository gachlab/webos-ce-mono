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
// Wiring the store into Qt's application proxy is a separate step; see the
// service README.
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

inline std::string proxiesConfigPayload(const std::vector<ProxyInfo>& list)
{
    std::string out = "{\"returnValue\":true,\"proxyInfoList\":[";
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
