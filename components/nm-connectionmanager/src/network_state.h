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

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

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
    // Ethernet only: whether a cable is physically in the socket, from NM's
    // Wired.Carrier. It is not the same question as "connected" -- a cable can
    // be in with the connection taken down -- and the difference is what makes
    // the system menu's row worth tapping or not. Measured with the cable out:
    // State 20, Carrier false, and a connect attempt refused by NM itself with
    // "because device has no carrier".
    bool carrier = false;

    bool activated() const { return present && state == kDeviceActivated; }
};

struct NetworkState {
    int connectivity = kConnectivityUnknown;
    Device wifi;
    Device wired;
    bool vpnActive = false;
    // NetworkManager's WirelessEnabled: the radio switch webOS's setstate
    // flips. Off, the device drops to "unavailable" and the indicator must go
    // out rather than read as a radio that is on and joined to nothing.
    bool wifiEnabled = true;
    // Why the wifi device is in its state (NM_DEVICE_STATE_REASON_*), which is
    // what says whether a failed join was a wrong password.
    int wifiStateReason = 0;
    // The saved profile the wifi device is using, 0 when none; see profileIdOf.
    int wifiProfileId = 0;
    // The network the last com.palm.wifi/connect asked for, kept by the service
    // until the device comes up. A failed join leaves the device with no access
    // point, and enyo's wifi library ignores a failure that does not name the
    // network it was joining.
    std::string attemptedSsid;
    // The access point the radio is joined to: its hardware address and the
    // frequency it is on, in MHz. The settings card names both.
    std::string wifiBssid;
    int wifiFrequency = 0;
};

// Whether any transport webOS knows about is carrying traffic.
inline bool anyTransportUp(const NetworkState& state)
{
    return state.wifi.activated() || state.wired.activated();
}

// Whether anything at all can reach the internet. A captive portal is
// deliberately NOT internet: the email app would otherwise keep trying to sync
// against the portal's login page, which is the failure this ticket exists to
// end.
//
// A transport is required on top of NM's own verdict, and that is not belt and
// braces -- it was measured. Switching wifi off, NetworkManager kept reporting
// Connectivity FULL for about two seconds after the device was already gone, so
// the payload said "no wifi, no cable, and the internet is available". That
// contradiction is not harmless: luna-sysservice acts on this field, and the
// apps spend those seconds syncing against a network that is no longer there.
//
// The cost of the rule is a machine online through something this service does
// not model -- mobile broadband, a bridge -- reading as offline. webOS has
// nowhere to put such a connection anyway: its consumers ask about wifi and wan,
// and answering "online" while every interface it knows reads disconnected is
// the same contradiction pointing the other way.
inline bool internetAvailable(const NetworkState& state)
{
    return state.connectivity == kConnectivityFull && anyTransportUp(state);
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
    out += ",\"carrier\":";
    out += state.wired.carrier ? "true" : "false";
    out += "}";

    // Always present, always disconnected: ConnectionManagerProxy reads
    // wan.state directly and a missing "wan" leaves its requirement unset.
    out += ",\"wan\":{\"state\":\"disconnected\"}";

    out += ",\"vpn\":{\"state\":\"";
    out += state.vpnActive ? "connected" : "disconnected";
    out += "\"}}";
    return out;
}

// --- com.palm.wifi ----------------------------------------------------------
//
// The status bar's wifi indicator is driven entirely by this service, not by
// com.palm.connectionmanager above: StatusBarServicesConnector subscribes to
// com.palm.wifi/getstatus and switches on a top-level "status" string. Nothing
// in the CE drop provides the name at all, which is why the indicator has never
// moved in this port.
//
// The vocabulary is HP's and is matched with strcmp, so these strings are not a
// choice either:
//
//   status "serviceDisabled"          radio off: the icon goes out
//          "serviceEnabled"           radio on, nothing joined
//          "connectionStateChanged"   with networkInfo below
//   networkInfo.connectState
//          "associating"/"associated" the connecting icon
//          "ipConfigured"             connected, and the bars are drawn
//          "notAssociated", "ipFailed", "associationFailed"
//                                     joined nothing, plain wifi icon
//
// TWO TRAPS IN THAT HANDLER, both of which shape what is sent here.
//
// It does `m_wifiSSID = std::string(ssid)` on the raw pointer for the
// associating, associated and ipConfigured branches, with no null check -- the
// same class of crash as the connectionmanager payload's. So "ssid" is always
// written as a string for those states, empty if the network has no name yet.
//
// And it indexes the icon enum out of range. For connectionStateChanged it
// computes WIFI_BAR_1 + clamp(signalBars - 1, 1, 3), and StatusBar.h ends at
// WIFI_BAR_3 = WIFI_BAR_1 + 2, so any signalBars of 4 or more selects an icon
// that does not exist. signalBars is therefore capped at 3 here. (Its
// "signalStrengthChanged" branch has the same arithmetic without the -1, so
// even a 3 overruns there; that status is deliberately never sent -- the
// connectionStateChanged path already draws the bars.)

// NM_DEVICE_STATE_*, the rest of the values a wifi device passes through.
enum WifiDeviceState {
    kDeviceDisconnected = 30,
    kDevicePrepare      = 40,
    kDeviceConfig       = 50,
    kDeviceNeedAuth     = 60,
    kDeviceIpConfig     = 70,
    kDeviceIpCheck      = 80,
    kDeviceSecondaries  = 90,
    kDeviceDeactivating = 110,
    kDeviceFailed       = 120,
};

inline const char* wifiConnectState(const Device& device)
{
    if (device.state == kDeviceActivated)
        return "ipConfigured";
    if (device.state >= kDevicePrepare && device.state <= kDeviceNeedAuth)
        return "associating";
    // Associated with the access point, still settling the address. HP draws
    // the same connecting icon for both.
    if (device.state >= kDeviceIpConfig && device.state <= kDeviceSecondaries)
        return "associated";
    if (device.state == kDeviceFailed)
        return "associationFailed";
    return "notAssociated";
}

// NM_DEVICE_STATE_REASON_*, the ones a failed join is told apart by.
enum DeviceStateReason {
    kReasonNoSecrets           = 7,
    kReasonSupplicantDisconnect = 8,
    kReasonSupplicantTimeout   = 11,
    kReasonSsidNotFound        = 53,
};

// The error enyo's wifi library shows for a failed join. Its vocabulary is
// fixed -- assocFailureString switches on "ApNotFound" and "IncorrectPasskey",
// and anything else is "Unable to connect".
//
// A wrong WPA password does not arrive as such: NetworkManager reports the
// supplicant disconnecting during the handshake, or asking for secrets again,
// or timing out, depending on the access point. All three are read as the
// password, which is what they almost always are on a personal network.
inline const char* lastConnectError(int reason)
{
    switch (reason) {
    case kReasonNoSecrets:
    case kReasonSupplicantDisconnect:
    case kReasonSupplicantTimeout:
        return "IncorrectPasskey";
    case kReasonSsidNotFound:
        return "ApNotFound";
    default:
        return "AssociationFailed";
    }
}

// The 802.11 channel of a frequency in MHz, 0 when it is none of the bands.
inline int channelOf(int mhz)
{
    if (mhz == 2484)
        return 14;
    if (mhz >= 2412 && mhz <= 2472)
        return (mhz - 2407) / 5;
    if (mhz >= 5955 && mhz <= 7115)
        return (mhz - 5950) / 5;
    if (mhz >= 5000 && mhz <= 5900)
        return (mhz - 5000) / 5;
    return 0;
}

// Whether the device's last join failed. NetworkManager passes through FAILED
// and settles on DISCONNECTED within the same second, keeping the reason, and
// the service reads the state once per burst of signals -- so FAILED is often
// never seen. A disconnected device whose reason is a join failure, while a
// join was being attempted, is the same failure.
inline bool joinFailed(const NetworkState& state)
{
    if (state.wifi.state == kDeviceFailed)
        return true;
    if (state.wifi.state != kDeviceDisconnected || state.attemptedSsid.empty())
        return false;
    switch (state.wifiStateReason) {
    case kReasonNoSecrets:
    case kReasonSupplicantDisconnect:
    case kReasonSupplicantTimeout:
    case kReasonSsidNotFound:
        return true;
    default:
        return false;
    }
}

// 1..3, capped for the reason above. The thresholds are the same ones
// confidence() uses, so the bars and the confidence level never disagree.
inline int signalBars(int strength)
{
    if (strength >= 75)
        return 3;
    if (strength >= 50)
        return 2;
    return 1;
}

// What com.palm.wifi/getstatus answers. Three shapes, because the status bar
// acts on three different top-level statuses.
inline std::string wifiStatusPayload(const NetworkState& state, bool subscribed)
{
    std::string out = "{\"returnValue\":true,\"subscribed\":";
    out += subscribed ? "true" : "false";

    // No wifi hardware at all is the same thing, as far as the indicator is
    // concerned, as a radio that has been switched off.
    if (!state.wifi.present || !state.wifiEnabled) {
        out += ",\"status\":\"serviceDisabled\"}";
        return out;
    }

    const bool failed = joinFailed(state);

    // Up but joined to nothing, and not on the way to joining anything: the
    // icon is on and empty. Sent as serviceEnabled rather than as a
    // notAssociated connectionStateChanged because that is the status HP uses
    // to mean exactly this, and it also clears the remembered ssid.
    if (state.wifi.state == kDeviceDisconnected && !failed) {
        out += ",\"status\":\"serviceEnabled\"}";
        return out;
    }

    out += ",\"status\":\"connectionStateChanged\",\"networkInfo\":{\"connectState\":\"";
    out += failed ? "associationFailed" : wifiConnectState(state.wifi);
    // Unconditional, and a string even when empty: the handler assigns it
    // without a null check on three of its branches. While joining, the access
    // point may not be known yet, and after a failure it is gone: the name the
    // join asked for stands in.
    out += "\",\"ssid\":\"";
    out += jsonEscape(state.wifi.ssid.empty() ? state.attemptedSsid : state.wifi.ssid);
    out += "\"";
    // enyo's wifi library asks for the profile of the network it just joined,
    // and marks a failed join with the reason it failed.
    if (state.wifiProfileId > 0)
        out += ",\"profileId\":" + std::to_string(state.wifiProfileId);
    if (failed) {
        out += ",\"lastConnectError\":\"";
        out += lastConnectError(state.wifiStateReason);
        out += "\"";
    }
    if (state.wifi.activated()) {
        char bars[64];
        std::snprintf(bars, sizeof bars, ",\"signalBars\":%d,\"signalLevel\":%d",
                      signalBars(state.wifi.strength), state.wifi.strength);
        out += bars;
        if (!state.wifi.ipAddress.empty())
            out += ",\"ipAddress\":\"" + jsonEscape(state.wifi.ipAddress) + "\"";
    }
    out += "}";
    // Beside networkInfo, where enyo's wifi library looks for it: the settings
    // card's connected view reads "BSSID ..., Channel ..." out of it.
    if (state.wifi.activated() && !state.wifiBssid.empty()) {
        out += ",\"apInfo\":{\"bssid\":\"" + jsonEscape(state.wifiBssid) + "\",\"channel\":"
               + std::to_string(channelOf(state.wifiFrequency)) + "}";
    }
    out += "}";
    return out;
}

// The network the radio is joined to, or empty.
inline std::string joinedSsid(const NetworkState& state)
{
    return (state.wifiEnabled && state.wifi.activated()) ? state.wifi.ssid : std::string();
}

// The update that says a network was left, or empty when none was.
//
// getstatus describes one network at a time, and its listeners -- the system
// menu's drawer and enyo's wifi library -- change only the row of the network
// each update names. Going from one network straight to joining another, the
// row of the one left behind kept its tick: measured on the settings card, two
// networks read as connected until the next scan replaced the list. HP's
// service said so first; this is that update, sent before the new state.
inline std::string leftNetworkPayload(const std::string& previouslyJoined, const NetworkState& now)
{
    if (previouslyJoined.empty())
        return std::string();
    if (joinedSsid(now) == previouslyJoined)
        return std::string();
    return "{\"returnValue\":true,\"subscribed\":true,\"status\":\"connectionStateChanged\","
           "\"networkInfo\":{\"connectState\":\"notAssociated\",\"ssid\":\""
           + jsonEscape(previouslyJoined) + "\"}}";
}

// What a wifi update is actually about.
//
// The payload carries signalLevel because enyo's wifi library reads it, and a
// laptop's signal wanders a percent at a time: measured in a running session,
// 84, then 80, then 79 within fifteen seconds, each one a different payload and
// so each one an update pushed to every subscriber. The status bar redraws the
// indicator for all of them, and with signalBars capped at 3 it cannot even
// show the difference.
//
// So what counts as a change is decided here rather than by comparing payloads:
// the state the device is in, the network's name and address, and the bars that
// are actually drawn. The level still goes out with every update -- it is simply
// not a reason to send one.
inline std::string wifiChangeKey(const NetworkState& state)
{
    if (!state.wifi.present)
        return "absent";
    if (!state.wifiEnabled)
        return "off";
    std::string key = std::to_string(state.wifi.state);
    key += joinFailed(state) ? "|failed:" + state.attemptedSsid : "|";
    key += '|';
    key += std::to_string(state.wifiProfileId);
    key += '|';
    key += state.wifiBssid;
    key += '|';
    key += state.wifi.ssid;
    key += '|';
    key += state.wifi.ipAddress;
    key += '|';
    key += std::to_string(state.wifi.activated() ? signalBars(state.wifi.strength) : 0);
    return key;
}


// --- com.palm.wifi, phase 3: scanning, joining, profiles ---------------------
//
// The callers, measured: the system menu's wifi drawer (findnetworks, connect,
// setstate) and enyo's wifi library, which com.palm.app.wifi is built on
// (all of those plus getprofile, deleteprofile and getinfo).

// What an access point asks for. Personal WPA and SAE are one choice to the
// user -- a password -- and one "securityType" to webOS; they differ only in
// what NetworkManager is told, see keyManagement().
enum Security {
    kSecurityNone,
    kSecurityWep,
    kSecurityWpaPsk,
    kSecuritySae,
    kSecurityEnterprise,
};

// NM_802_11_AP_FLAGS_PRIVACY and NM_802_11_AP_SEC_KEY_MGMT_*.
enum ApSecurityFlags {
    kApPrivacy       = 0x1,
    kApKeyMgmtPsk    = 0x100,
    kApKeyMgmt8021x  = 0x200,
    kApKeyMgmtSae    = 0x400,
};

// From an access point's Flags, WpaFlags and RsnFlags. Enterprise wins because
// a network that offers 802.1X cannot be joined with a password alone; PSK wins
// over SAE because a transition-mode network offers both and PSK is the one
// every supplicant speaks.
inline Security apSecurity(unsigned flags, unsigned wpaFlags, unsigned rsnFlags)
{
    const unsigned keyMgmt = wpaFlags | rsnFlags;
    if (keyMgmt & kApKeyMgmt8021x)
        return kSecurityEnterprise;
    if (keyMgmt & kApKeyMgmtPsk)
        return kSecurityWpaPsk;
    if (keyMgmt & kApKeyMgmtSae)
        return kSecuritySae;
    if (flags & kApPrivacy)
        return kSecurityWep;
    return kSecurityNone;
}

// HP's names. An open network has none: enyo tells an open network by the
// field being absent, not by a value.
inline const char* securityType(Security security)
{
    switch (security) {
    case kSecurityWep:        return "wep";
    case kSecurityWpaPsk:
    case kSecuritySae:        return "wpa-personal";
    case kSecurityEnterprise: return "enterprise";
    default:                  return nullptr;
    }
}

// What NetworkManager's 802-11-wireless-security.key-mgmt must say.
inline const char* keyManagement(Security security)
{
    switch (security) {
    case kSecurityWep:    return "none";
    case kSecurityWpaPsk: return "wpa-psk";
    case kSecuritySae:    return "sae";
    default:              return nullptr;
    }
}

// webOS identifies a saved network by an integer. NetworkManager identifies a
// connection by an object path ending in one, /org/freedesktop/NetworkManager/
// Settings/12, so that number is the profileId. It is not stable across
// NetworkManager restarts, and it does not need to be: nothing in webOS keeps
// one longer than the screen that asked for it.
inline int profileIdOf(const std::string& settingsPath)
{
    static const std::string prefix = "/org/freedesktop/NetworkManager/Settings/";
    if (settingsPath.compare(0, prefix.size(), prefix) != 0)
        return 0;
    const std::string digits = settingsPath.substr(prefix.size());
    if (digits.empty() || digits.size() > 9
        || digits.find_first_not_of("0123456789") != std::string::npos)
        return 0;
    return std::stoi(digits);
}

inline std::string settingsPathOf(int profileId)
{
    return "/org/freedesktop/NetworkManager/Settings/" + std::to_string(profileId);
}

struct AccessPoint {
    std::string ssid;
    int strength = 0;
    Security security = kSecurityNone;
    int profileId = 0;          // a saved profile for this ssid, 0 if none
    bool active = false;        // the one the device is joined or joining to
};

// One entry per network name, as HP's list had: an office with six access
// points is one network to choose. The strongest access point speaks for the
// name. Hidden networks (no name) are left out -- they are joined by typing the
// name -- and the joined network comes first, then the rest by signal.
inline std::vector<AccessPoint> mergeScan(const std::vector<AccessPoint>& raw)
{
    std::vector<AccessPoint> out;
    for (const AccessPoint& ap : raw) {
        if (ap.ssid.empty())
            continue;
        auto same = std::find_if(out.begin(), out.end(),
                                 [&](const AccessPoint& seen) { return seen.ssid == ap.ssid; });
        if (same == out.end()) {
            out.push_back(ap);
            continue;
        }
        const bool active = same->active || ap.active;
        const int profileId = same->profileId ? same->profileId : ap.profileId;
        if (ap.strength > same->strength)
            *same = ap;
        same->active = active;
        same->profileId = profileId;
    }
    std::stable_sort(out.begin(), out.end(), [](const AccessPoint& a, const AccessPoint& b) {
        if (a.active != b.active)
            return a.active;
        return a.strength > b.strength;
    });
    return out;
}

// com.palm.wifi/findnetworks. Every field read by the menu's handler is typed
// there -- ssid and securityType as strings, profileId and signalBars as ints --
// and one of the wrong type is silently skipped, so each is written only with
// its right type and only when it means something.
inline std::string foundNetworksPayload(const std::vector<AccessPoint>& networks,
                                        const NetworkState& state)
{
    std::string out = "{\"returnValue\":true,\"foundNetworks\":[";
    bool first = true;
    for (const AccessPoint& ap : networks) {
        if (!first)
            out += ',';
        first = false;
        char numbers[64];
        std::snprintf(numbers, sizeof numbers, ",\"signalBars\":%d,\"signalLevel\":%d",
                      signalBars(ap.strength), ap.strength);
        out += "{\"networkInfo\":{\"ssid\":\"" + jsonEscape(ap.ssid) + "\"";
        out += numbers;
        if (const char* type = securityType(ap.security))
            out += std::string(",\"securityType\":\"") + type + "\"";
        if (ap.profileId > 0)
            out += ",\"profileId\":" + std::to_string(ap.profileId);
        // The joined network says how far it got, which is what the menu uses to
        // draw it as connected and to send a tap on it to the settings app
        // instead of joining it again.
        if (ap.active && state.wifi.present) {
            out += std::string(",\"connectState\":\"") + wifiConnectState(state.wifi) + "\"";
            if (joinFailed(state))
                out += std::string(",\"lastConnectError\":\"")
                       + lastConnectError(state.wifiStateReason) + "\"";
        }
        out += "}}";
    }
    out += "]}";
    return out;
}

// A saved wifi profile, as getprofile answers it.
struct Profile {
    int profileId = 0;
    std::string ssid;
    Security security = kSecurityNone;
    bool staticIp = false;
};

// The address the device holds, for the IP settings screen.
struct IpInfo {
    std::string ip;
    std::string subnet;
    std::string gateway;
    std::string dns1;
    std::string dns2;
};

// A prefix length as the dotted mask the screen shows.
inline std::string subnetMask(int prefix)
{
    if (prefix < 0 || prefix > 32)
        return std::string();
    const unsigned long mask = prefix == 0 ? 0UL : (0xffffffffUL << (32 - prefix)) & 0xffffffffUL;
    char out[16];
    std::snprintf(out, sizeof out, "%lu.%lu.%lu.%lu",
                  (mask >> 24) & 0xff, (mask >> 16) & 0xff, (mask >> 8) & 0xff, mask & 0xff);
    return out;
}

// com.palm.wifi/getprofile. The library reads the profile from "wifiProfile"
// and the address from a top-level "ipInfo", which it only has when the profile
// is the one in use.
inline std::string profilePayload(const Profile& profile, const IpInfo* ip)
{
    std::string out = "{\"returnValue\":true,\"wifiProfile\":{\"profileId\":";
    out += std::to_string(profile.profileId);
    out += ",\"ssid\":\"" + jsonEscape(profile.ssid) + "\"";
    if (const char* type = securityType(profile.security))
        out += std::string(",\"securityType\":\"") + type + "\"";
    out += ",\"useStaticIp\":";
    out += profile.staticIp ? "true" : "false";
    out += "}";
    if (ip) {
        out += ",\"ipInfo\":{\"ip\":\"" + jsonEscape(ip->ip) + "\",\"subnet\":\""
               + jsonEscape(ip->subnet) + "\",\"gateway\":\"" + jsonEscape(ip->gateway) + "\"";
        if (!ip->dns1.empty())
            out += ",\"dns1\":\"" + jsonEscape(ip->dns1) + "\"";
        if (!ip->dns2.empty())
            out += ",\"dns2\":\"" + jsonEscape(ip->dns2) + "\"";
        out += "}";
    }
    out += "}";
    return out;
}

// com.palm.wifi/getprofilelist: every saved wifi profile, in HP's shape --
// the security nested under "security", absent for an open network.
inline std::string profileListPayload(const std::vector<Profile>& profiles)
{
    std::string out = "{\"returnValue\":true,\"profileList\":[";
    for (size_t i = 0; i < profiles.size(); ++i) {
        const Profile& p = profiles[i];
        if (i)
            out += ',';
        out += "{\"wifiProfile\":{\"profileId\":" + std::to_string(p.profileId);
        out += ",\"ssid\":\"" + jsonEscape(p.ssid) + "\"";
        if (const char* type = securityType(p.security))
            out += std::string(",\"security\":{\"securityType\":\"") + type + "\"}";
        out += "}}";
    }
    out += "]}";
    return out;
}

// com.palm.wifi/getinfo. "wapi" is a Chinese standard the library offers extra
// security choices for when it reads "enabled"; NetworkManager has no WAPI.
inline std::string infoPayload(const std::string& macAddress)
{
    return "{\"returnValue\":true,\"wifiInfo\":{\"macAddress\":\"" + jsonEscape(macAddress)
           + "\",\"wapi\":\"disabled\"}}";
}

inline std::string errorPayload(const std::string& text)
{
    return "{\"returnValue\":false,\"errorText\":\"" + jsonEscape(text) + "\"}";
}

// What com.palm.wifi/connect was asked for, once parsed.
struct ConnectRequest {
    int profileId = 0;
    std::string ssid;
    std::string securityType;   // HP's name, empty for an open network
    std::string passKey;
    int keyIndex = 0;           // WEP only, 0..3
    bool isInHex = false;
    bool hidden = false;        // wasCreatedWithJoinOther
    bool staticIp = false;      // useStaticIp or ipInfo was sent
};

inline bool allHex(const std::string& s)
{
    return !s.empty() && s.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
}

// Why a request cannot be carried out, or empty when it can. Checked here
// rather than left to NetworkManager because NM's answer to a bad key is a
// connection that fails some seconds later, while the user is still looking at
// the field they typed it in.
inline std::string validateConnect(const ConnectRequest& req)
{
    if (req.staticIp)
        return "static IP settings are not supported yet";
    if (req.profileId > 0)
        return std::string();
    if (req.profileId < 0)
        return "profileId must be positive";
    if (req.ssid.empty())
        return "expected a profileId or an ssid";
    if (req.ssid.size() > 32)
        return "an ssid is at most 32 bytes";
    const std::string& type = req.securityType;
    if (type.empty() || type == "none")
        return std::string();
    if (type == "wpa-personal") {
        const size_t n = req.passKey.size();
        if (n == 64 && allHex(req.passKey))
            return std::string();
        if (n < 8 || n > 63)
            return "a WPA password is 8 to 63 characters";
        return std::string();
    }
    if (type == "wep") {
        const size_t n = req.passKey.size();
        if (req.keyIndex < 0 || req.keyIndex > 3)
            return "a WEP key index is 0 to 3";
        if ((n == 10 || n == 26) && allHex(req.passKey))
            return std::string();
        if (n == 5 || n == 13)
            return std::string();
        return "a WEP key is 5 or 13 characters, or 10 or 26 hex digits";
    }
    if (type == "enterprise")
        return "enterprise networks are not supported yet";
    return "unsupported security type: " + type;
}

// The Security a request asks for, given what the access point advertises.
// The access point decides between PSK and SAE, which the user cannot see; with
// no scan result the user's choice stands and PSK is assumed.
inline Security requestedSecurity(const ConnectRequest& req, Security advertised)
{
    if (req.securityType == "wep")
        return kSecurityWep;
    if (req.securityType == "wpa-personal")
        return advertised == kSecuritySae ? kSecuritySae : kSecurityWpaPsk;
    return kSecurityNone;
}


// --- When Device Sleeps -------------------------------------------------------
//
// The settings card's "When Device Sleeps: Keep Wi-Fi On / Turn Wi-Fi Off",
// which HP's card read and wrote through com.palm.connectionmanager's
// getWakeOnWiFiMode and setWakeOnWiFiMode, with "enable" and "disable". On the
// phone it was the radio's own sleep mode. Here it is what a laptop can do with
// it: with "disable", the radio is switched off as the machine suspends and on
// again as it resumes -- and only if it was on, so a radio the user had
// switched off stays off.

enum class SleepRadio { Nothing, TurnOff, TurnOn };

// What to do with the radio at logind's PrepareForSleep. turnedOff remembers,
// between the two, whether the radio was switched off for this sleep.
inline SleepRadio sleepRadioAction(bool keepOnWhileAsleep, bool goingToSleep, bool radioOn,
                                   bool& turnedOff)
{
    if (goingToSleep) {
        if (keepOnWhileAsleep || !radioOn)
            return SleepRadio::Nothing;
        turnedOff = true;
        return SleepRadio::TurnOff;
    }
    if (!turnedOff)
        return SleepRadio::Nothing;
    turnedOff = false;
    return SleepRadio::TurnOn;
}

inline const char* wakeOnWifiMode(bool keepOnWhileAsleep)
{
    return keepOnWhileAsleep ? "enable" : "disable";
}

// "enable" or "disable" into keepOnWhileAsleep; false for anything else.
inline bool parseWakeOnWifiMode(const std::string& mode, bool& keepOnWhileAsleep)
{
    if (mode == "enable") {
        keepOnWhileAsleep = true;
        return true;
    }
    if (mode == "disable") {
        keepOnWhileAsleep = false;
        return true;
    }
    return false;
}

inline std::string wakeOnWifiPayload(bool keepOnWhileAsleep)
{
    return std::string("{\"returnValue\":true,\"mode\":\"") + wakeOnWifiMode(keepOnWhileAsleep) + "\"}";
}

}  // namespace NmNet

#endif  // NM_CONNECTIONMANAGER_NETWORK_STATE_H
