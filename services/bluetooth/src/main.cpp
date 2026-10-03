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
// com.palm.btmonitor and com.palm.bluetooth, answered from BlueZ.
//
// HP's own services for these names shipped only on the device and are not in
// this tree, so the specification is the one surviving consumer: the system
// menu's Bluetooth drawer in reference/luna-sysmgr-ce's
// StatusBarServicesConnector.cpp. It was read request by request (see
// bluez_state.h), and the payloads here answer exactly what it parses, so the
// radio toggles and the device list fills without the shell being touched.
//
// What BlueZ 5 offers that HP's BR/EDR stack did not -- LE discovery with a
// transport filter, modern pairing through an Agent1 (PIN, passkey, numeric
// comparison), battery level -- is carried through for the card (apps/bluetooth,
// #31), not thrown away to fit the old contract.
//
// Why C++ and not JavaScript, the same reason as nm-connectionmanager: the
// D-Bus client gio already provides to anything linking glib, versus bundling
// one. The mapping from BlueZ's state to webOS's payloads is in bluez_state.h,
// free of both buses; what is read from BlueZ and asked of it is in
// bluez_client.cpp, run against a fake BlueZ on a private bus by
// tests/bluez-client.py.
//

#include "bluez_client.h"
#include "bluez_state.h"
#include "bluez_agent.h"

#include <luna-service2/lunaservice.h>

#include <cjson/json.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib-unix.h>

#include <map>
#include <memory>
#include <string>

namespace {

const char kBtMonitorName[] = "com.palm.btmonitor";
const char kBluetoothName[] = "com.palm.bluetooth";
const char kBtMonitorCategory[] = "/monitor";
const char kBluetoothGapCategory[] = "/gap";
const char kBluetoothProfCategory[] = "/prof";

// BlueZ on the system bus.
const char kBluez[] = "org.bluez";

GMainLoop* g_loop = nullptr;
LSPalmService* g_btmonitor = nullptr;
LSPalmService* g_bluetooth = nullptr;
GDBusConnection* g_system = nullptr;

// The state last read from BlueZ, kept so each push can diff against it and the
// radio transition (on->turningoff, off->turningon) can be told apart -- a
// single snapshot cannot, which is why bluez_state.h takes the Radio in.
BtState::BluetoothState g_state;
bool g_lastPowered = false;
std::string g_lastTrusted;
std::string g_lastProfiles;
guint g_refreshPending = 0;

void logAndFree(const char* where, LSError& error)
{
    g_warning("bluetooth: %s: %s", where, error.message ? error.message : "(no message)");
    LSErrorFree(&error);
}

// --- parsing webOS requests -------------------------------------------------

// One string field out of a request payload, or "". The bus speaks JSON and
// cjson is what every service here parses it with.
std::string stringField(LSMessage* message, const char* key)
{
    const char* payload = LSMessageGetPayload(message);
    if (!payload)
        return std::string();
    std::string out;
    json_object* root = json_tokener_parse(payload);
    if (root && !is_error(root)) {
        json_object* label = json_object_object_get(root, key);
        if (label && json_object_is_type(label, json_type_string)) {
            const char* s = json_object_get_string(label);
            if (s)
                out = s;
        }
        json_object_put(root);
    }
    return out;
}

bool boolField(LSMessage* message, const char* key, bool fallback)
{
    const char* payload = LSMessageGetPayload(message);
    if (!payload)
        return fallback;
    bool out = fallback;
    json_object* root = json_tokener_parse(payload);
    if (root && !is_error(root)) {
        json_object* label = json_object_object_get(root, key);
        if (label && json_object_is_type(label, json_type_boolean))
            out = json_object_get_boolean(label);
        json_object_put(root);
    }
    return out;
}

void reply(LSHandle* sh, LSMessage* message, const std::string& payload)
{
    LSError error;
    LSErrorInit(&error);
    if (!LSMessageReply(sh, message, payload.c_str(), &error))
        logAndFree("LSMessageReply", error);
}

// --- telling webOS: subscription posts --------------------------------------

void postOn(LSPalmService* service, const char* category, const char* method,
            const std::string& payload)
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
        LSError error;
        LSErrorInit(&error);
        if (!LSSubscriptionPost(handle, category, method, payload.c_str(), &error))
            logAndFree("LSSubscriptionPost", error);
    }
}

// --- the state, read and diffed ---------------------------------------------
//
// BlueZ's Adapter1.Powered is a settled boolean; it has no "turning on" state
// to read. So the transition strings HP's connector shows a spinner for
// (turningon/turningoff, notifnradioturningon) are emitted by the radioon /
// radiooff handlers at the moment the service asks BlueZ to flip the radio --
// which is the only point anything here knows a change is in flight. refresh()
// then emits the settled on/off once BlueZ's PropertiesChanged confirms it.

// Find a device by address in a state snapshot, or nullptr.
const BtState::Device* deviceByAddress(const BtState::BluetoothState& state,
                                       const std::string& address)
{
    for (const BtState::Device& device : state.devices) {
        if (device.address == address)
            return &device;
    }
    return nullptr;
}

// Emit the discrete connect/disconnect notifications the status-bar connector
// acts on. HP's bluetoothEventsCallback keys entirely off a "notification"
// field (notifnconnected/notifndisconnected/...), so the full-state push the
// gap/prof channels also carry is not enough to move the icon -- the connector
// drops a payload with no "notification". For every device whose Connected
// changed since the last snapshot, post one event per menu profile it carries,
// keyed to that profile and address (which is how the connector indexes
// m_bluetoothProfileStates). A rename is deliberately not emitted: HP's
// notifndevrenamed branch reads "address" with no null guard, so emitting it
// without an address would crash the shell.
void postConnectionEvents(const BtState::BluetoothState& prev,
                          const BtState::BluetoothState& next)
{
    for (const BtState::Device& device : next.devices) {
        const BtState::Device* before = deviceByAddress(prev, device.address);
        const bool wasConnected = before && before->connected;
        if (wasConnected == device.connected)
            continue;   // no connection change for this device

        const BtState::ProfileEvent event = device.connected
            ? BtState::ProfileEvent::kConnected
            : BtState::ProfileEvent::kDisconnected;
        const std::vector<std::string> profiles = BtState::menuProfilesOf(device);
        // A device with no menu profile (e.g. a HID-only gamepad) still toggles
        // the icon under HP's logic only through a menu profile, so there is
        // nothing to post for it -- the trusted-list push already carries its
        // status for the card.
        for (const std::string& profile : profiles) {
            postOn(g_bluetooth, kBluetoothProfCategory, "subscribenotifications",
                   BtState::profileNotificationPayload(event, profile, device.address,
                                                       device.name));
        }
    }
    // A device that vanished entirely (unpaired/removed) while connected: tell
    // the connector it disconnected so the icon clears.
    for (const BtState::Device& device : prev.devices) {
        if (!device.connected)
            continue;
        if (deviceByAddress(next, device.address))
            continue;
        for (const std::string& profile : BtState::menuProfilesOf(device)) {
            postOn(g_bluetooth, kBluetoothProfCategory, "subscribenotifications",
                   BtState::profileNotificationPayload(BtState::ProfileEvent::kDisconnected,
                                                       profile, device.address, device.name));
        }
    }
}

void refresh()
{
    const BtState::BluetoothState next = BtClient::readState(g_system);
    const bool poweredNow = next.adapter.present && next.adapter.powered;

    // btmonitor: push the settled radio state when it changed. turningon/
    // turningoff were already pushed by the radioon/radiooff handlers; this is
    // the on/off that follows once BlueZ confirms Powered.
    if (poweredNow != g_lastPowered) {
        postOn(g_btmonitor, kBtMonitorCategory, "subscribenotifications",
               BtState::radioPayload(poweredNow ? BtState::Radio::kOn : BtState::Radio::kOff, true));
        g_lastPowered = poweredNow;
    }

    // The discrete connect/disconnect events the status-bar icon follows, from
    // the per-device diff against the last snapshot. Done before g_state is
    // replaced, since it is the "before".
    postConnectionEvents(g_state, next);

    // bluetooth gap: push the trusted list when it changed. The card reads this
    // (names, battery, icons); the connector ignores it, which is why the
    // discrete events above exist.
    const std::string trusted = BtState::trustedDevicesPayload(next);
    if (trusted != g_lastTrusted) {
        postOn(g_bluetooth, kBluetoothGapCategory, "subscribenotifications", trusted);
        g_lastTrusted = trusted;
    }

    // bluetooth prof: push the full profile state when it changed, for the card
    // and for a connector re-reading profgetstate. The discrete events above
    // are what actually move the status-bar icon.
    const std::string profiles = BtState::profileStatePayload(next, "all");
    if (profiles != g_lastProfiles) {
        postOn(g_bluetooth, kBluetoothProfCategory, "subscribenotifications", profiles);
        g_lastProfiles = profiles;
    }

    g_state = next;
}

gboolean refreshNow(gpointer)
{
    g_refreshPending = 0;
    refresh();
    return G_SOURCE_REMOVE;
}

// Changes from BlueZ arrive in bursts -- pairing a device adds an object and
// then rewrites a dozen of its properties -- so one read per burst, not one per
// signal.
void scheduleRefresh()
{
    if (!g_refreshPending)
        g_refreshPending = g_idle_add(refreshNow, nullptr);
}

void onBluezSignal(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                   const gchar*, GVariant*, gpointer)
{
    scheduleRefresh();
}

// --- btmonitor methods ------------------------------------------------------

bool btmonitorSubscribe(LSHandle* sh, LSMessage* message, void*)
{
    bool subscribed = false;
    LSError error;
    LSErrorInit(&error);
    if (!LSSubscriptionProcess(sh, message, &subscribed, &error))
        logAndFree("LSSubscriptionProcess", error);

    const bool powered = g_state.adapter.present && g_state.adapter.powered;
    reply(sh, message, BtState::radioPayload(powered ? BtState::Radio::kOn
                                                     : BtState::Radio::kOff, subscribed));
    return true;
}

bool radioOn(LSHandle* sh, LSMessage* message, void*)
{
    // HP's radioon carries {"visible","connectable"}; default to the discreet
    // pairing the system menu asks for (not discoverable, connectable).
    const bool visible = boolField(message, "visible", false);
    const bool connectable = boolField(message, "connectable", true);
    std::string err;
    const bool ok = BtClient::setPowered(g_system, true, visible, connectable, err);
    reply(sh, message, BtState::radioResultPayload(ok, err));
    // The radio is now on its way on: tell the drawer so it shows the spinner.
    // The settled "on" follows from refresh() when BlueZ confirms Powered.
    if (ok)
        postOn(g_btmonitor, kBtMonitorCategory, "subscribenotifications",
               BtState::radioPayload(BtState::Radio::kTurningOn, true));
    scheduleRefresh();
    return true;
}

bool radioOff(LSHandle* sh, LSMessage* message, void*)
{
    std::string err;
    const bool ok = BtClient::setPowered(g_system, false, false, false, err);
    reply(sh, message, BtState::radioResultPayload(ok, err));
    if (ok)
        postOn(g_btmonitor, kBtMonitorCategory, "subscribenotifications",
               BtState::radioPayload(BtState::Radio::kTurningOff, true));
    scheduleRefresh();
    return true;
}

// --- bluetooth gap methods --------------------------------------------------

bool getTrustedDevices(LSHandle* sh, LSMessage* message, void*)
{
    g_state = BtClient::readState(g_system);
    reply(sh, message, BtState::trustedDevicesPayload(g_state));
    return true;
}

bool gapSubscribe(LSHandle* sh, LSMessage* message, void*)
{
    bool subscribed = false;
    LSError error;
    LSErrorInit(&error);
    if (!LSSubscriptionProcess(sh, message, &subscribed, &error))
        logAndFree("LSSubscriptionProcess", error);
    reply(sh, message, BtState::trustedDevicesPayload(g_state));
    return true;
}

bool startDiscovery(LSHandle* sh, LSMessage* message, void*)
{
    // transport is modern: "le" | "bredr" | "auto". Absent means leave BlueZ's
    // filter as it is, which is HP's unfiltered classic+LE scan.
    const std::string transport = stringField(message, "transport");
    std::string err;
    const bool ok = BtClient::startDiscovery(g_system, transport, err);
    reply(sh, message, BtState::radioResultPayload(ok, err));
    scheduleRefresh();
    return true;
}

bool stopDiscovery(LSHandle* sh, LSMessage* message, void*)
{
    std::string err;
    const bool ok = BtClient::stopDiscovery(g_system, err);
    reply(sh, message, BtState::radioResultPayload(ok, err));
    scheduleRefresh();
    return true;
}

bool pair(LSHandle* sh, LSMessage* message, void*)
{
    const std::string address = stringField(message, "address");
    std::string err;
    const bool ok = BtClient::pairDevice(g_system, address, err);
    reply(sh, message, BtState::radioResultPayload(ok, err));
    scheduleRefresh();
    return true;
}

bool cancelPairing(LSHandle* sh, LSMessage* message, void*)
{
    const std::string address = stringField(message, "address");
    // Return any prompt the agent is holding (a passkey/PIN the card dismissed)
    // right away, rather than waiting for BlueZ to drive Agent1.Cancel back to
    // us -- the card has already left the prompt. Then ask BlueZ to abort the
    // pairing itself.
    BtAgent::cancel();
    std::string err;
    const bool ok = BtClient::cancelPairing(g_system, address, err);
    reply(sh, message, BtState::radioResultPayload(ok, err));
    return true;
}

bool removeDevice(LSHandle* sh, LSMessage* message, void*)
{
    const std::string address = stringField(message, "address");
    std::string err;
    const bool ok = BtClient::removeDevice(g_system, address, err);
    reply(sh, message, BtState::radioResultPayload(ok, err));
    scheduleRefresh();
    return true;
}

bool setTrusted(LSHandle* sh, LSMessage* message, void*)
{
    const std::string address = stringField(message, "address");
    const bool trusted = boolField(message, "trusted", true);
    std::string err;
    const bool ok = BtClient::setTrusted(g_system, address, trusted, err);
    reply(sh, message, BtState::radioResultPayload(ok, err));
    scheduleRefresh();
    return true;
}

// --- the pairing agent's prompts and the card's answers ---------------------

const char* promptKindString(BtAgent::PromptKind kind)
{
    switch (kind) {
    case BtAgent::PromptKind::kRequestPinCode:      return "requestpincode";
    case BtAgent::PromptKind::kRequestPasskey:      return "requestpasskey";
    case BtAgent::PromptKind::kDisplayPinCode:      return "displaypincode";
    case BtAgent::PromptKind::kDisplayPasskey:      return "displaypasskey";
    case BtAgent::PromptKind::kRequestConfirmation: return "requestconfirmation";
    case BtAgent::PromptKind::kRequestAuthorization: return "requestauthorization";
    }
    return "";
}

// BlueZ asked the user something mid-pairing. Push it to whoever is subscribed
// to gap notifications -- the card renders the screen and answers with one of
// the supply* methods below.
void onPairingPrompt(const BtAgent::Prompt& prompt)
{
    const std::string payload = BtState::pairingPromptPayload(
        promptKindString(prompt.kind), prompt.deviceAddress, prompt.deviceName,
        prompt.passkey, prompt.pinCode);
    postOn(g_bluetooth, kBluetoothGapCategory, "subscribenotifications", payload);
}

bool supplyConfirmation(LSHandle* sh, LSMessage* message, void*)
{
    const bool accept = boolField(message, "accept", false);
    const bool ok = BtAgent::resolveConfirmation(accept);
    reply(sh, message, BtState::radioResultPayload(ok, ok ? "" : "no pairing awaiting confirmation"));
    scheduleRefresh();
    return true;
}

bool supplyPasskey(LSHandle* sh, LSMessage* message, void*)
{
    const std::string passkey = stringField(message, "passkey");
    const bool ok = BtAgent::resolvePasskey(passkey);
    reply(sh, message, BtState::radioResultPayload(ok, ok ? "" : "no pairing awaiting a passkey"));
    scheduleRefresh();
    return true;
}

bool supplyPinCode(LSHandle* sh, LSMessage* message, void*)
{
    const std::string pinCode = stringField(message, "pincode");
    const bool ok = BtAgent::resolvePinCode(pinCode);
    reply(sh, message, BtState::radioResultPayload(ok, ok ? "" : "no pairing awaiting a PIN"));
    scheduleRefresh();
    return true;
}

// --- bluetooth prof methods -------------------------------------------------
bool profGetState(LSHandle* sh, LSMessage* message, void*)
{
    const std::string profile = stringField(message, "profile");
    g_state = BtClient::readState(g_system);
    reply(sh, message, BtState::profileStatePayload(g_state, profile.empty() ? "all" : profile));
    return true;
}

bool profSubscribe(LSHandle* sh, LSMessage* message, void*)
{
    bool subscribed = false;
    LSError error;
    LSErrorInit(&error);
    if (!LSSubscriptionProcess(sh, message, &subscribed, &error))
        logAndFree("LSSubscriptionProcess", error);
    reply(sh, message, BtState::profileStatePayload(g_state, "all"));
    return true;
}

bool profConnect(LSHandle* sh, LSMessage* message, void*)
{
    const std::string profile = stringField(message, "profile");
    const std::string address = stringField(message, "address");
    std::string err;
    const bool ok = BtClient::connectProfile(g_system, address, profile, err);
    reply(sh, message, BtState::radioResultPayload(ok, err));
    scheduleRefresh();
    return true;
}

bool profDisconnect(LSHandle* sh, LSMessage* message, void*)
{
    const std::string profile = stringField(message, "profile");
    const std::string address = stringField(message, "address");
    std::string err;
    const bool ok = BtClient::disconnectProfile(g_system, address, profile, err);
    reply(sh, message, BtState::radioResultPayload(ok, err));
    scheduleRefresh();
    return true;
}

// --- method tables ----------------------------------------------------------

LSMethod kBtMonitorMethods[] = {
    { "subscribenotifications", btmonitorSubscribe },
    { "radioon", radioOn },
    { "radiooff", radioOff },
    { },
};

LSMethod kGapMethods[] = {
    { "gettrusteddevices", getTrustedDevices },
    { "subscribenotifications", gapSubscribe },
    { "startdiscovery", startDiscovery },
    { "stopdiscovery", stopDiscovery },
    { "pair", pair },
    { "cancelpairing", cancelPairing },
    { "removedevice", removeDevice },
    { "settrusted", setTrusted },
    { "supplyconfirmation", supplyConfirmation },
    { "supplypasskey", supplyPasskey },
    { "supplypincode", supplyPinCode },
    { },
};

LSMethod kProfMethods[] = {
    { "profgetstate", profGetState },
    { "subscribenotifications", profSubscribe },
    { "profconnect", profConnect },
    { "profdisconnect", profDisconnect },
    { },
};

gboolean quit(gpointer)
{
    g_main_loop_quit(g_loop);
    return G_SOURCE_REMOVE;
}

bool registerService(const char* name, LSPalmService** service, const char* category,
                     LSMethod* methods)
{
    LSError error;
    LSErrorInit(&error);
    if (!LSRegisterPalmService(name, service, &error)) {
        logAndFree("LSRegisterPalmService", error);
        return false;
    }
    LSErrorInit(&error);
    if (!LSPalmServiceRegisterCategory(*service, category, methods, methods,
                                       nullptr, nullptr, &error)) {
        logAndFree("LSPalmServiceRegisterCategory", error);
        return false;
    }
    LSErrorInit(&error);
    if (!LSGmainAttachPalmService(*service, g_loop, &error)) {
        logAndFree("LSGmainAttachPalmService", error);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    g_loop = g_main_loop_new(nullptr, FALSE);

    // The system bus, where BlueZ lives. If it is unreachable the service still
    // starts and reports a radio that is off, which keeps the drawer present
    // rather than making com.palm.btmonitor vanish from the bus.
    GError* gerror = nullptr;
    g_system = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &gerror);
    if (!g_system) {
        g_warning("bluetooth: no system bus: %s", gerror ? gerror->message : "(no message)");
        g_clear_error(&gerror);
    }

    // com.palm.btmonitor is the one the radio toggle needs; fatal if it cannot
    // be had.
    if (!registerService(kBtMonitorName, &g_btmonitor, kBtMonitorCategory, kBtMonitorMethods))
        return 1;

    // com.palm.bluetooth owns two categories, /gap and /prof. Register the
    // service, then its second category on the same handle.
    {
        LSError error;
        LSErrorInit(&error);
        if (!LSRegisterPalmService(kBluetoothName, &g_bluetooth, &error)) {
            logAndFree("LSRegisterPalmService(com.palm.bluetooth)", error);
            return 1;
        }
        LSErrorInit(&error);
        if (!LSPalmServiceRegisterCategory(g_bluetooth, kBluetoothGapCategory, kGapMethods,
                                           kGapMethods, nullptr, nullptr, &error)) {
            logAndFree("register /gap", error);
            return 1;
        }
        LSErrorInit(&error);
        if (!LSPalmServiceRegisterCategory(g_bluetooth, kBluetoothProfCategory, kProfMethods,
                                           kProfMethods, nullptr, nullptr, &error)) {
            logAndFree("register /prof", error);
            return 1;
        }
        LSErrorInit(&error);
        if (!LSGmainAttachPalmService(g_bluetooth, g_loop, &error)) {
            logAndFree("LSGmainAttachPalmService(com.palm.bluetooth)", error);
            return 1;
        }
    }

    if (g_system) {
        // Everything BlueZ says about its objects. InterfacesAdded/Removed is a
        // device appearing or going; PropertiesChanged carries Powered,
        // Connected, Trusted and the rest. Reading once per burst (scheduleRefresh)
        // keeps a pairing's dozen property signals to one read.
        g_dbus_connection_signal_subscribe(
            g_system, kBluez, "org.freedesktop.DBus.ObjectManager",
            "InterfacesAdded", nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
            onBluezSignal, nullptr, nullptr);
        g_dbus_connection_signal_subscribe(
            g_system, kBluez, "org.freedesktop.DBus.ObjectManager",
            "InterfacesRemoved", nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
            onBluezSignal, nullptr, nullptr);
        g_dbus_connection_signal_subscribe(
            g_system, kBluez, "org.freedesktop.DBus.Properties",
            "PropertiesChanged", nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
            onBluezSignal, nullptr, nullptr);
    }

    // The pairing agent: an org.bluez.Agent1 registered as BlueZ's default, so
    // pairing goes through the card rather than bluetoothctl's terminal. Not
    // fatal if it fails -- Just Works pairings still succeed without it.
    if (g_system)
        BtAgent::start(g_system, onPairingPrompt);

    // The state before anyone can ask, and the last-seen values with it, so the
    // first real change is what gets posted rather than a duplicate of this.
    g_state = BtClient::readState(g_system);
    g_lastPowered = g_state.adapter.present && g_state.adapter.powered;
    g_lastTrusted = BtState::trustedDevicesPayload(g_state);
    g_lastProfiles = BtState::profileStatePayload(g_state, "all");
    g_message("bluetooth: com.palm.btmonitor and com.palm.bluetooth up, radio=%s, %zu device(s)",
              g_lastPowered ? "on" : "off", g_state.devices.size());

    g_unix_signal_add(SIGTERM, quit, nullptr);
    g_unix_signal_add(SIGINT, quit, nullptr);

    g_main_loop_run(g_loop);

    if (g_system)
        BtAgent::stop(g_system);

    LSError error;
    LSErrorInit(&error);
    if (g_btmonitor && !LSUnregisterPalmService(g_btmonitor, &error))
        logAndFree("LSUnregisterPalmService(btmonitor)", error);
    LSErrorInit(&error);
    if (g_bluetooth && !LSUnregisterPalmService(g_bluetooth, &error))
        logAndFree("LSUnregisterPalmService(bluetooth)", error);
    if (g_system)
        g_object_unref(g_system);
    g_main_loop_unref(g_loop);
    return 0;
}
