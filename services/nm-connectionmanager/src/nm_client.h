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
// Everything this service says to NetworkManager, and nothing it says to webOS.
//
// Every call takes the D-Bus connection it should use rather than reaching for
// the system bus itself. main.cpp hands it the system bus; tests/nm-client.cpp
// hands it a private bus with a fake NetworkManager on it, so what is read from
// NM and what is asked of it are checked without the host's network and without
// ls-hubd. network_state.h is the other half: what the state read here becomes
// on the webOS bus.
//

#ifndef NM_CLIENT_H
#define NM_CLIENT_H

#include "network_state.h"

#include <gio/gio.h>

#include <string>
#include <vector>

namespace NmClient {

// The machine's network as NetworkManager reports it. A null connection, or one
// where NetworkManager does not answer, reads as a disconnected machine -- which
// is the truth as far as anything here can tell.
NmNet::NetworkState readState(GDBusConnection* bus);

// Connects or disconnects the cable. On failure, error holds NetworkManager's
// own message, or "no wired device" when the machine has no ethernet socket.
bool setWired(GDBusConnection* bus, bool connected, std::string& error);

// --- wifi -------------------------------------------------------------------
// Every call below fails with "no wifi device" on a machine without one, and
// otherwise with NetworkManager's own message.

// Switches the radio: NetworkManager's WirelessEnabled.
bool setWifiEnabled(GDBusConnection* bus, bool enabled, std::string& error);

// The networks in range, one entry per name, with any saved profile for each.
// Asks NetworkManager to scan again as well; the list returned is the one it
// already has, and the fresh scan reaches the next call.
bool scan(GDBusConnection* bus, std::vector<NmNet::AccessPoint>& networks, std::string& error);

// Joins a network: a saved profile by id, or a network by name, creating its
// profile -- or replacing the security of the one that exists -- when a key is
// given. profileId is the profile used. The request is expected to have passed
// NmNet::validateConnect.
bool connectWifi(GDBusConnection* bus, const NmNet::ConnectRequest& request,
                 int& profileId, std::string& error);

// A saved wifi profile, and the address in use when it is the active one
// (active says which). Profiles that are not wifi -- the cable, a VPN -- are
// refused: this is com.palm.wifi, and it must not hand out or delete those.
bool getProfile(GDBusConnection* bus, int profileId, NmNet::Profile& profile,
                NmNet::IpInfo& ip, bool& active, std::string& error);
bool deleteProfile(GDBusConnection* bus, int profileId, std::string& error);

// Every saved wifi profile, in NetworkManager's order.
bool listProfiles(GDBusConnection* bus, std::vector<NmNet::Profile>& profiles, std::string& error);

// The wifi adapter's hardware address.
bool wifiMacAddress(GDBusConnection* bus, std::string& mac, std::string& error);

// --- vpn (com.palm.vpn) -----------------------------------------------------
// Saved NetworkManager connections of type "vpn" (OpenVPN) or "wireguard".
// Profiles that are wifi or ethernet are refused: this is not com.palm.wifi.

bool listVpnProfiles(GDBusConnection* bus, std::vector<NmNet::VpnProfile>& profiles,
                     std::string& error);

bool getVpnProfile(GDBusConnection* bus, const std::string& name, NmNet::VpnProfile& profile,
                   std::string& error);

bool addVpnProfile(GDBusConnection* bus, const NmNet::VpnRequest& request, std::string& error);

bool updateVpnProfile(GDBusConnection* bus, const NmNet::VpnRequest& request, std::string& error);

bool deleteVpnProfile(GDBusConnection* bus, const std::string& name, std::string& error);

bool connectVpn(GDBusConnection* bus, const std::string& name, std::string& error);

bool disconnectVpn(GDBusConnection* bus, std::string& error);

} // namespace NmClient

#endif
