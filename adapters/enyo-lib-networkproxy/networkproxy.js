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
// enyo's lib/networkproxy, which HP's released lib/wifi depends on and HP never
// released. Without it enyo stops loading lib/wifi's dependencies at this one,
// and every kind declared after it -- the whole Wi-Fi settings card -- is never
// defined.
//
// lib/wifi reaches two functions. openProxyConfigUi is behind a "Configure
// Proxy" button; removeProxyConfig runs when a network is forgotten. Both talk
// to com.palm.connectionmanager and the Networking card (#23).
//

var NetworkProxyConfigLib = {
	openProxyConfigUi: function (proxyConfig, owner) {
		var technology = (proxyConfig && proxyConfig.networkTechnology) || "wifi";
		var scope = proxyConfig && proxyConfig.proxyScope;
		if (scope === undefined || scope === null || scope === "") {
			console.log("NetworkProxyConfigLib: proxyScope required");
			return;
		}
		var bridge = new PalmServiceBridge();
		bridge.onservicecallback = function () {};
		bridge.call("palm://com.palm.applicationManager/open",
			JSON.stringify({
				id: "com.palm.app.network",
				params: {
					mode: "proxy",
					networkTechnology: technology,
					proxyScope: String(scope)
				}
			}));
	},
	removeProxyConfig: function (proxyConfig, owner) {
		var technology = (proxyConfig && proxyConfig.networkTechnology) || "wifi";
		var scope = proxyConfig && proxyConfig.proxyScope;
		if (scope === undefined || scope === null || scope === "") {
			return;
		}
		var bridge = new PalmServiceBridge();
		bridge.onservicecallback = function () {};
		bridge.call("palm://com.palm.connectionmanager/configureNwProxies",
			JSON.stringify({
				action: "rmv",
				proxyInfo: {
					networkTechnology: technology,
					proxyScope: String(scope)
				}
			}));
	}
};
