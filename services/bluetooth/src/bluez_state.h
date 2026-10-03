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

#ifndef WEBOS_BLUETOOTH_BLUEZ_STATE_H
#define WEBOS_BLUETOOTH_BLUEZ_STATE_H

//
// What BlueZ says about the adapter and its devices, and the payloads
// com.palm.btmonitor and com.palm.bluetooth have to answer with. Header-only
// and free of both buses on purpose, so every decision here can be tested
// without a D-Bus daemon and without ls-hubd -- the way network_state.h is.
//
// The field names on the webOS side are not a choice. Each one is read by HP's
// StatusBarServicesConnector.cpp (reference/luna-sysmgr-ce), which is the only
// surviving specification of the API, since HP's own service is not in this
// tree. The connector was read request by request, not guessed:
//
//   btmonitor subscribenotifications -> {"radio": "on"|"turningon"|
//       "turningoff"|"off"} and/or {"notification": "notifnradioturningon"|
//       "notifnradioon"|"notifnradiooff"}
//   bluetooth gap/gettrusteddevices  -> {"trusteddevices":[{address,name,
//       status,cod}]}
//   bluetooth prof/profgetstate      -> per profile [{state,address,name}]
//   bluetooth prof/subscribenotifications -> {"notification":"notifn...",
//       profile,address,name,error}
//
// The seven profile names HP uses are its own vocabulary, not BlueZ's: hfg a2dp
// pan hid spp hf mapc. BlueZ speaks 128-bit SIG UUIDs, so profileFromUuid below
// is the bridge, and the UUIDs were confirmed against real devices on the
// development machine (an AKG headset reports 0000110b/0000110e/0000111e).
//
// What modern BlueZ offers that HP's BR/EDR stack did not is carried through to
// the card, not thrown away to fit the old contract: addressType (LE), rssi,
// appearance, battery percentage, bonded, and BlueZ's resolved icon. The system
// menu never reads those; the card does.
//

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace BtState {

// --- the webOS side: HP's profile vocabulary -------------------------------
//
// The order is HP's own (StatusBarServicesConnector resizes to 7 and fills
// them in this order). The four the system menu shows connection status for are
// hfg, a2dp, hf and mapc; the card lists the rest too.
inline const std::vector<std::string>& hpProfiles()
{
    static const std::vector<std::string> profiles = {
        "hfg", "a2dp", "pan", "hid", "spp", "hf", "mapc",
    };
    return profiles;
}

// The four profiles the system menu shows connection status for, as HP's
// m_bluetoothMenuProfiles. A connect/disconnect notification only moves the
// status-bar icon when its profile is one of these, so the service emits its
// notifn* events keyed to the menu profiles a device actually carries.
inline const std::vector<std::string>& menuProfiles()
{
    static const std::vector<std::string> profiles = { "hfg", "a2dp", "hf", "mapc" };
    return profiles;
}

// The 16-bit SIG service class behind each HP profile name, as the low 32 bits
// of the 128-bit UUID ("0000XXXX-0000-1000-8000-00805f9b34fb"). A device
// advertises the services it has; a profile is "present" on a device when the
// device carries the matching UUID.
//
//   hfg  Handsfree Audio Gateway   0000111f   (the phone/computer side)
//   hf   Handsfree                 0000111e   (the headset side)
//   a2dp A2DP Sink or Source       0000110b / 0000110a
//   pan  PANU or NAP               00001115 / 00001116
//   hid  Human Interface Device    00001124
//   spp  Serial Port               00001101
//   mapc Message Access Client     00001134   (MAP server 00001132)
//
// profileFromUuid returns the HP name for a UUID, or "" when the UUID is not one
// HP named. a2dp and pan and mapc each have two UUIDs that both map to the one
// HP profile.
inline std::string profileFromUuid(const std::string& uuid)
{
    // Compare only the 16-bit short id, case-insensitively, ignoring the SIG
    // base. A full-length UUID has the short id at [4,8); anything shorter is
    // not a SIG UUID this cares about.
    if (uuid.size() < 8) {
        return "";
    }
    std::string shortId = uuid.substr(4, 4);
    std::transform(shortId.begin(), shortId.end(), shortId.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (shortId == "111f") return "hfg";
    if (shortId == "111e") return "hf";
    if (shortId == "110b" || shortId == "110a") return "a2dp";
    if (shortId == "1115" || shortId == "1116") return "pan";
    if (shortId == "1124") return "hid";
    if (shortId == "1101") return "spp";
    if (shortId == "1134" || shortId == "1132") return "mapc";
    return "";
}

// The canonical SIG UUID to ask BlueZ to connect for an HP profile name, or ""
// when the name is not one HP's connector sends. The inverse of
// profileFromUuid for the connectable direction: a device carries a2dp as a
// sink (110b) but is connected as such through the SIG profile UUID, so this
// returns the one ConnectProfile expects. "all" has no single UUID and is the
// caller's signal to connect the whole device instead.
inline std::string uuidForProfile(const std::string& profile)
{
    const char* shortId = nullptr;
    if (profile == "hfg")  shortId = "111f";
    else if (profile == "hf")   shortId = "111e";
    else if (profile == "a2dp") shortId = "110b";
    else if (profile == "pan")  shortId = "1116";
    else if (profile == "hid")  shortId = "1124";
    else if (profile == "spp")  shortId = "1101";
    else if (profile == "mapc") shortId = "1134";
    if (!shortId) {
        return "";
    }
    return std::string("0000") + shortId + "-0000-1000-8000-00805f9b34fb";
}

// --- what BlueZ reports -----------------------------------------------------

// org.bluez.Device1, the fields read here. A device BlueZ has an object for,
// whether or not it is connected or even in range.
struct Device {
    std::string objectPath;     // /org/bluez/hci0/dev_XX_...
    std::string address;        // Device1.Address, "F8:DF:15:F2:29:ED"
    std::string name;           // Device1.Alias (falls back to Name)
    std::uint32_t cod = 0;      // Device1.Class, HP's "cod"
    bool connected = false;     // Device1.Connected
    bool paired = false;        // Device1.Paired
    bool trusted = false;       // Device1.Trusted
    bool bonded = false;        // Device1.Bonded (modern; card only)
    std::vector<std::string> uuids;  // Device1.UUIDs, the services it carries

    // Modern, for the card. Absent fields keep their sentinel and are omitted
    // from the payload rather than sent as a lie.
    std::string addressType;    // "public" | "random" (LE)
    std::string icon;           // BlueZ's resolved icon, e.g. "audio-headset"
    int rssi = kNoRssi;         // Device1.RSSI, dBm; only while discovering
    int battery = kNoBattery;   // Battery1.Percentage, 0..100
    int appearance = kNoAppearance;  // Device1.Appearance (LE)

    static constexpr int kNoRssi = 127;        // out of the int16 RSSI range
    static constexpr int kNoBattery = -1;
    static constexpr int kNoAppearance = -1;
};

// The menu profiles (hfg/a2dp/hf/mapc) a device carries, by its UUIDs. These
// are the profiles a connect/disconnect notification is keyed to, so the
// status-bar icon follows the device: a headset carrying A2DP and Handsfree
// yields {"a2dp","hf"}. Order follows menuProfiles() so the result is stable.
inline std::vector<std::string> menuProfilesOf(const Device& device)
{
    std::vector<std::string> out;
    for (const std::string& profile : menuProfiles()) {
        for (const std::string& uuid : device.uuids) {
            if (profileFromUuid(uuid) == profile) {
                out.push_back(profile);
                break;
            }
        }
    }
    return out;
}

// org.bluez.Adapter1. A machine may have none (no adapter object at all), which
// reads as a radio that is off and cannot be turned on.
struct Adapter {
    bool present = false;
    bool powered = false;       // Adapter1.Powered
    bool discovering = false;   // Adapter1.Discovering
    bool discoverable = false;  // Adapter1.Discoverable
    bool pairable = false;      // Adapter1.Pairable
    std::string address;
    std::string name;           // Adapter1.Alias (falls back to Name)
};

// The whole stack as one value, which is what the service keeps and diffs to
// decide what to push.
struct BluetoothState {
    Adapter adapter;
    std::vector<Device> devices;
};

// --- btmonitor: the radio ---------------------------------------------------

// The "radio" string btmonitor/subscribenotifications reports. HP's connector
// keys off exactly these four; "turningon"/"turningoff" are the transitions,
// which only the service (watching PropertiesChanged) can tell apart, so they
// are passed in rather than read from a single state snapshot.
enum class Radio { kOff, kTurningOn, kTurningOff, kOn };

inline const char* radioString(Radio radio)
{
    switch (radio) {
    case Radio::kOn:         return "on";
    case Radio::kTurningOn:  return "turningon";
    case Radio::kTurningOff: return "turningoff";
    case Radio::kOff:        return "off";
    }
    return "off";
}

// The notification string that pairs with a radio state. HP reads notifnradioon
// / notifnradiooff / notifnradioturningon; there is no notifnradioturningoff in
// the connector, so a turning-off transition carries only the "radio" field.
inline const char* radioNotification(Radio radio)
{
    switch (radio) {
    case Radio::kOn:        return "notifnradioon";
    case Radio::kTurningOn: return "notifnradioturningon";
    case Radio::kOff:       return "notifnradiooff";
    case Radio::kTurningOff: return nullptr;
    }
    return nullptr;
}

// --- JSON, built as a string the way network_state.h does -------------------

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

// The connection status HP's connector reads out of each device: "connected"
// when BlueZ says Connected, "disconnected" otherwise. "connecting" and
// "disconnecting" are transient and come from the notification stream, not from
// a resting snapshot like gettrusteddevices.
inline const char* connectionStatus(const Device& device)
{
    return device.connected ? "connected" : "disconnected";
}

// btmonitor/subscribenotifications: a radio-state push. Both "radio" and
// "notification" when there is a notification for the state, "radio" alone for
// a turning-off transition.
inline std::string radioPayload(Radio radio, bool subscribed)
{
    std::string out = "{\"returnValue\":true,\"subscribed\":";
    out += subscribed ? "true" : "false";
    out += ",\"radio\":\"";
    out += radioString(radio);
    out += "\"";
    const char* notif = radioNotification(radio);
    if (notif) {
        out += ",\"notification\":\"";
        out += notif;
        out += "\"";
    }
    out += "}";
    return out;
}

// btmonitor/radioon and radiooff reply with just returnValue; the push above is
// what carries the state. A failure keeps the daemon's own message.
inline std::string radioResultPayload(bool success, const std::string& error = "")
{
    if (success) {
        return "{\"returnValue\":true}";
    }
    return "{\"returnValue\":false,\"errorText\":\"" + jsonEscape(error) + "\"}";
}

// --- bluetooth gap/gettrusteddevices ----------------------------------------

// One trusted-device entry, exactly the four fields HP's connector pulls out
// (address, name, status, cod), plus the modern fields the card reads and the
// system menu ignores.
inline std::string trustedDeviceEntry(const Device& device)
{
    std::string out = "{\"address\":\"" + jsonEscape(device.address) + "\"";
    out += ",\"name\":\"" + jsonEscape(device.name) + "\"";
    out += ",\"status\":\"";
    out += connectionStatus(device);
    out += "\",\"cod\":";
    out += std::to_string(device.cod);
    // Modern, for the card. Omitted when absent rather than sent as a sentinel.
    out += ",\"paired\":";
    out += device.paired ? "true" : "false";
    if (!device.icon.empty()) {
        out += ",\"icon\":\"" + jsonEscape(device.icon) + "\"";
    }
    if (device.battery != Device::kNoBattery) {
        out += ",\"battery\":" + std::to_string(device.battery);
    }
    if (!device.addressType.empty()) {
        out += ",\"addressType\":\"" + jsonEscape(device.addressType) + "\"";
    }
    out += "}";
    return out;
}

// gettrusteddevices: every device BlueZ marks Trusted. HP's connector filters
// again by class on its side, so the service does not have to; it answers all
// trusted devices and lets the menu keep the ones it shows.
inline std::string trustedDevicesPayload(const BluetoothState& state)
{
    std::string out = "{\"returnValue\":true,\"trusteddevices\":[";
    bool first = true;
    for (const Device& device : state.devices) {
        if (!device.trusted) {
            continue;
        }
        if (!first) {
            out += ",";
        }
        first = false;
        out += trustedDeviceEntry(device);
    }
    out += "]}";
    return out;
}

// --- bluetooth prof/profgetstate --------------------------------------------

// profgetstate {"profile":"all"}: for each HP profile name, an array of the
// connected devices that carry it. HP's connector reads root[profileName] as an
// array of {state,address,name}. A device appears under a profile when it is
// connected and its UUIDs include that profile's service.
inline std::string profileStatePayload(const BluetoothState& state,
                                        const std::string& requested)
{
    std::string out = "{\"returnValue\":true";
    for (const std::string& profile : hpProfiles()) {
        if (requested != "all" && requested != profile) {
            continue;
        }
        out += ",\"" + profile + "\":[";
        bool first = true;
        for (const Device& device : state.devices) {
            if (!device.connected) {
                continue;
            }
            bool carries = false;
            for (const std::string& uuid : device.uuids) {
                if (profileFromUuid(uuid) == profile) {
                    carries = true;
                    break;
                }
            }
            if (!carries) {
                continue;
            }
            if (!first) {
                out += ",";
            }
            first = false;
            out += "{\"state\":\"connected\",\"address\":\""
                   + jsonEscape(device.address) + "\",\"name\":\""
                   + jsonEscape(device.name) + "\"}";
        }
        out += "]";
    }
    out += "}";
    return out;
}

// --- bluetooth prof/subscribenotifications ----------------------------------

// The notification strings HP's connector keys off, one per connection
// transition. notifnconnectacceptrequest exists in the connector but is a
// no-op there, so it is not produced.
enum class ProfileEvent {
    kConnecting, kConnected, kDisconnecting, kDisconnected,
    kDeviceRenamed, kDeviceRemoved,
};

inline const char* profileEventString(ProfileEvent event)
{
    switch (event) {
    case ProfileEvent::kConnecting:     return "notifnconnecting";
    case ProfileEvent::kConnected:      return "notifnconnected";
    case ProfileEvent::kDisconnecting:  return "notifndisconnecting";
    case ProfileEvent::kDisconnected:   return "notifndisconnected";
    case ProfileEvent::kDeviceRenamed:  return "notifndevrenamed";
    case ProfileEvent::kDeviceRemoved:  return "notifndevremoved";
    }
    return "";
}

// A prof/gap notification push. HP reads notification, profile, address, name
// and (on a connect) error. error is 0 for success; a non-zero error on a
// notifnconnected is how the connector learns a connect failed.
inline std::string profileNotificationPayload(ProfileEvent event,
                                               const std::string& profile,
                                               const std::string& address,
                                               const std::string& name,
                                               int error = 0)
{
    std::string out = "{\"returnValue\":true,\"notification\":\"";
    out += profileEventString(event);
    out += "\"";
    if (!profile.empty()) {
        out += ",\"profile\":\"" + jsonEscape(profile) + "\"";
    }
    if (!address.empty()) {
        out += ",\"address\":\"" + jsonEscape(address) + "\"";
    }
    if (!name.empty()) {
        out += ",\"name\":\"" + jsonEscape(name) + "\"";
    }
    out += ",\"error\":" + std::to_string(error);
    out += "}";
    return out;
}

// --- bluetooth gap: a pairing prompt from the agent -------------------------

// The agent (bluez_agent.cpp) pushes one of these when BlueZ asks the user
// something during pairing. The card renders a screen per "prompt" kind and
// answers with gap/supplyconfirmation, supplypasskey or supplypincode. The
// kinds are the agent's PromptKind, as the strings the card switches on.
inline std::string pairingPromptPayload(const std::string& kind,
                                        const std::string& address,
                                        const std::string& name,
                                        const std::string& passkey,
                                        const std::string& pinCode)
{
    std::string out = "{\"returnValue\":true,\"notification\":\"notifnpairingrequest\"";
    out += ",\"prompt\":\"" + jsonEscape(kind) + "\"";
    out += ",\"address\":\"" + jsonEscape(address) + "\"";
    out += ",\"name\":\"" + jsonEscape(name) + "\"";
    if (!passkey.empty()) {
        out += ",\"passkey\":\"" + jsonEscape(passkey) + "\"";
    }
    if (!pinCode.empty()) {
        out += ",\"pincode\":\"" + jsonEscape(pinCode) + "\"";
    }
    out += "}";
    return out;
}

} // namespace BtState

#endif
