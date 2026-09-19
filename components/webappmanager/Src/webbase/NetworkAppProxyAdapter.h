// The Networking card's per-network proxies, as Qt's application proxy.
//
// com.palm.connectionmanager keeps the store (#23). Web pages and the browser
// run in this process on QtWebEngine, which takes QNetworkProxy::applicationProxy
// (manual host:port) and, before the engine starts, Chromium's --proxy-pac-url
// for PAC. ADAPTER: none of HP's code is involved; WebAppManager only installs it.

#ifndef NETWORKAPPPROXYADAPTER_H
#define NETWORKAPPPROXYADAPTER_H

#include <functional>
#include <string>
#include <vector>

#include <QJsonObject>

#include "network_proxies.h"

struct LSHandle;

namespace NetworkAppProxy {

using Apply = std::function<void(const NmNet::AppProxy& proxy)>;

// Default: QNetworkProxy::setApplicationProxy + chromium PAC flag when needed.
void applyWithQt(const NmNet::AppProxy& proxy);

// Tests inject a recorder; install() restores applyWithQt.
void setApply(Apply apply);

// Forget the last applied proxy so the next consider() always runs apply.
void reset();

// Recompute from the active wifi profileId and the current proxy list.
void consider(int wifiProfileId, const std::vector<NmNet::ProxyInfo>& list);

// wifi.profileId from a connectionmanager getstatus payload; 0 if idle.
int wifiProfileIdOf(const QJsonObject& status);

// proxyInfoList from a getNwProxiesConfig payload.
std::vector<NmNet::ProxyInfo> proxiesOf(const QJsonObject& reply);

// Subscribe to getstatus + getNwProxiesConfig on com.palm.connectionmanager.
void install(LSHandle* handle);

} // namespace NetworkAppProxy

#endif
