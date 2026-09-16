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

#ifndef NM_CONNECTIONMANAGER_NETWORK_STATE_H
#define NM_CONNECTIONMANAGER_NETWORK_STATE_H

//
// What NetworkManager says about the network, and the payload
// com.palm.connectionmanager/getstatus has to answer with. Header-only and free
// of both buses on purpose, so every decision here can be tested without a
// D-Bus daemon and without ls-hubd.
//
// The field names are not a choice. Each one is read by a caller that acts on
// it, and the callers were measured rather than guessed:
//
//   isInternetConnectionAvailable   luna-sysservice's NetworkConnectionListener
//                                   declares it REQUIRED in its schema and drops
//                                   the whole payload without it;
//                                   BrowserServer and the status bar read it.
//   wifi.state, wifi.onInternet     StatusBarServicesConnector.cpp, to decide
//                                   whether to hide the WAN icon.
//   wifi/wan .state                 activitymanager's ConnectionManagerProxy,
//                                   which turns them into the "wifi" and "wan"
//                                   activity requirements.
//   networkConfidenceLevel          the same proxy, for the *Confidence
//                                   requirements. It compares against exactly
//                                   four strings -- "none", "poor", "fair",
//                                   "excellent" -- and anything else lands as
//                                   ConnectionConfidenceUnknown.
//
// ONE INVARIANT IS NOT NEGOTIABLE, and it is why `state` and `onInternet` are
// always written even when there is no wifi at all. StatusBarServicesConnector
// does this, with no null check:
//
//     if(!strcmp(state, "connected") && !strcmp(onInternet, "yes"))
//
// `state` and `onInternet` are the raw `const char*` it got out of the "wifi"
// object. Ship a "wifi" object without either one as a string and the shell
// dereferences a null pointer. A missing field here is not a wrong icon, it is
// a segfault, so tests/network-state.cpp asserts both are present in every
// single case.
//
// What is deliberately not answered: "wan" is always disconnected -- nothing
// here has a modem -- and "bridge", which HP's stub also sent, is left out
// because no caller in the tree reads it and inventing a value for it would be
// the stub's habit, not a measurement.
//

#include <cstdio>
#include <string>

namespace NmNet {

// org.freedesktop.NetworkManager's own enumerations, only the values read here.
// NM_DEVICE_TYPE_*: the machine this was written on reports 1 for enp0s31f6,
// 2 for wlp0s20f3, and also 13 (bridge, docker0), 16 (tun, tailscale0 and
// tun0), 20 (veth), 30 (wifi-p2p) and 32 (loopback) -- which is the reason the
// service picks devices by type instead of taking NM's PrimaryConnection: with
// a VPN up, the primary connection is the tunnel and the transport underneath
// it is what webOS actually wants to report.
enum DeviceType {
    kDeviceUnknown  = 0,
    kDeviceEthernet = 1,
    kDeviceWifi     = 2,
};

// NM_DEVICE_STATE_ACTIVATED. Anything below it is some stage of coming up or
// going down; measured: 20 (unavailable) is what an ethernet port with the
// cable out reports, alongside Wired.Carrier = false.
enum DeviceStateValue {
    kDeviceActivated = 100,
};

// NM_CONNECTIVITY_*. FULL is the only one that means the internet is actually
// reachable; PORTAL is the captive portal the ticket asks to be visible as
// such, and LIMITED/NONE are a network that carries no traffic out.
enum ConnectivityValue {
    kConnectivityUnknown = 0,
    kConnectivityNone    = 1,
    kConnectivityPortal  = 2,
    kConnectivityLimited = 3,
    kConnectivityFull    = 4,
};

struct Device {
    bool present = false;
    int state = 0;                 // NM_DEVICE_STATE_*
    std::string interfaceName;
    std::string ipAddress;
    std::string ssid;              // wifi only, empty otherwise
    int strength = -1;             // wifi only, 0..100; -1 when not applicable

    bool activated() const { return present && state == kDeviceActivated; }
};

struct NetworkState {
    int connectivity = kConnectivityUnknown;
    Device wifi;
    Device wired;
    bool vpnActive = false;
};

// Whether anything at all can reach the internet. A captive portal is
// deliberately NOT internet: the email app would otherwise keep trying to sync
// against the portal's login page, which is the failure this ticket exists to
// end.
inline bool internetAvailable(const NetworkState& state)
{
    return state.connectivity == kConnectivityFull;
}

inline const char* deviceState(const Device& device)
{
    return device.activated() ? "connected" : "disconnected";
}

// Per interface, not global: a device that is up on a network with no way out
// says "no" here while still being "connected" above, which is exactly how the
// status bar tells a usable wifi from a joined-but-useless one.
inline const char* onInternet(const Device& device, const NetworkState& state)
{
    return (device.activated() && internetAvailable(state)) ? "yes" : "no";
}

// Only four strings exist as far as activitymanager is concerned; see the note
// at the top. The thresholds follow NetworkManager's own four-bar convention
// (nmcli draws its bars at 80/55/30), shifted to leave "none" meaning unusable
// rather than merely weak. A wired link has no signal to speak of, so it is
// "excellent" whenever it is up.
inline const char* confidence(const Device& device)
{
    if (!device.activated())
        return "none";
    if (device.strength < 0)        // not a wifi device: nothing to measure
        return "excellent";
    if (device.strength >= 75)
        return "excellent";
    if (device.strength >= 50)
        return "fair";
    if (device.strength >= 25)
        return "poor";
    return "none";
}

// An SSID is whatever bytes the access point advertises, and it arrives from NM
// as a byte array rather than a string. One with a quote or a backslash in it
// would produce a payload that is not JSON at all, and every consumer drops the
// whole thing -- the network would read as absent instead of as badly named.
// Control characters get the \u form because a raw one is invalid inside a JSON
// string; bytes above 0x7f are passed through, since they are almost always
// UTF-8 already and mangling them would rename the network.
inline std::string jsonEscape(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size() + 8);
    for (unsigned char c : raw) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) {
                char escaped[7];
                std::snprintf(escaped, sizeof escaped, "\\u%04x", c);
                out += escaped;
            } else {
                out += static_cast<char>(c);
            }
            break;
        }
    }
    return out;
}

// The one object every caller reads. `subscribed` is what the caller asked for
// and got: LS2 expects it in the first reply to a subscribing call, and HP's
// stub always claimed true whether or not anyone could ever push an update.
inline std::string statusPayload(const NetworkState& state, bool subscribed)
{
    std::string out = "{\"returnValue\":true,\"subscribed\":";
    out += subscribed ? "true" : "false";
    out += ",\"isInternetConnectionAvailable\":";
    out += internetAvailable(state) ? "true" : "false";

    // wifi: state and onInternet unconditionally -- see the invariant above.
    out += ",\"wifi\":{\"state\":\"";
    out += deviceState(state.wifi);
    out += "\",\"onInternet\":\"";
    out += onInternet(state.wifi, state);
    out += "\",\"networkConfidenceLevel\":\"";
    out += confidence(state.wifi);
    out += "\"";
    if (!state.wifi.interfaceName.empty()) {
        out += ",\"interfaceName\":\"" + jsonEscape(state.wifi.interfaceName) + "\"";
    }
    if (!state.wifi.ipAddress.empty()) {
        out += ",\"ipAddress\":\"" + jsonEscape(state.wifi.ipAddress) + "\"";
    }
    if (!state.wifi.ssid.empty()) {
        out += ",\"ssid\":\"" + jsonEscape(state.wifi.ssid) + "\"";
    }
    out += "}";

    // wired: the same shape. HP's stub had no such key, because a phone had no
    // socket; the apps read "isInternetConnectionAvailable" and are satisfied,
    // and this is here so a cable is visible to anything that looks.
    out += ",\"wired\":{\"state\":\"";
    out += deviceState(state.wired);
    out += "\",\"onInternet\":\"";
    out += onInternet(state.wired, state);
    out += "\",\"networkConfidenceLevel\":\"";
    out += confidence(state.wired);
    out += "\"";
    if (!state.wired.interfaceName.empty()) {
        out += ",\"interfaceName\":\"" + jsonEscape(state.wired.interfaceName) + "\"";
    }
    if (!state.wired.ipAddress.empty()) {
        out += ",\"ipAddress\":\"" + jsonEscape(state.wired.ipAddress) + "\"";
    }
    out += "}";

    // Always present, always disconnected: ConnectionManagerProxy reads
    // wan.state directly and a missing "wan" leaves its requirement unset.
    out += ",\"wan\":{\"state\":\"disconnected\"}";

    out += ",\"vpn\":{\"state\":\"";
    out += state.vpnActive ? "connected" : "disconnected";
    out += "\"}}";
    return out;
}

}  // namespace NmNet

#endif  // NM_CONNECTIONMANAGER_NETWORK_STATE_H
