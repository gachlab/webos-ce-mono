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

#include "bluez_agent.h"
#include "bluez_client.h"

#include <glib.h>

#include <cerrno>
#include <cstdlib>

namespace BtAgent {

namespace {

const char kBluez[] = "org.bluez";
const char kAgentPath[] = "/org/webos/bluetooth/agent";
const char kAgentManagerIface[] = "org.bluez.AgentManager1";
// KeyboardDisplay is the capability that lets BlueZ choose any SSP method: a
// headset stays Just Works, a keyboard gets a passkey to display, two screens
// get numeric comparison. A narrower capability would make BlueZ reject pairings
// a laptop can perfectly well do.
const char kCapability[] = "KeyboardDisplay";

// org.bluez.Agent1, only the methods BlueZ calls on an agent. Release and Cancel
// take no reply; the Request/Display methods are left pending until the card
// answers, except the auto-accepted ones. The XML is the contract BlueZ
// introspects, so a method missing here is a pairing that hangs.
const char kIntrospection[] = R"XML(
<node>
  <interface name="org.bluez.Agent1">
    <method name="Release"/>
    <method name="RequestPinCode">
      <arg name="device" type="o" direction="in"/>
      <arg name="pincode" type="s" direction="out"/>
    </method>
    <method name="DisplayPinCode">
      <arg name="device" type="o" direction="in"/>
      <arg name="pincode" type="s" direction="in"/>
    </method>
    <method name="RequestPasskey">
      <arg name="device" type="o" direction="in"/>
      <arg name="passkey" type="u" direction="out"/>
    </method>
    <method name="DisplayPasskey">
      <arg name="device" type="o" direction="in"/>
      <arg name="passkey" type="u" direction="in"/>
      <arg name="entered" type="q" direction="in"/>
    </method>
    <method name="RequestConfirmation">
      <arg name="device" type="o" direction="in"/>
      <arg name="passkey" type="u" direction="in"/>
    </method>
    <method name="RequestAuthorization">
      <arg name="device" type="o" direction="in"/>
    </method>
    <method name="AuthorizeService">
      <arg name="device" type="o" direction="in"/>
      <arg name="uuid" type="s" direction="in"/>
    </method>
    <method name="Cancel"/>
  </interface>
</node>
)XML";

// Everything the agent keeps between a BlueZ call and the card's answer.
struct AgentState {
    GDBusConnection* bus = nullptr;
    guint registrationId = 0;
    OnPrompt onPrompt;

    // The one call BlueZ is waiting on. Only one pairing runs at a time -- the
    // adapter pairs serially -- so a single slot is enough. nullptr when idle.
    GDBusMethodInvocation* pending = nullptr;
    PromptKind pendingKind = PromptKind::kRequestConfirmation;
};

AgentState g_agent;

// The six-digit passkey BlueZ hands as a uint32, as the string the card shows.
// BlueZ's own UI pads it to six digits; so do we, since "comparing the digits"
// is the whole point of numeric comparison.
std::string passkeyString(guint32 passkey)
{
    char buf[16];
    g_snprintf(buf, sizeof buf, "%06u", passkey);
    return buf;
}

// The device's address and name from its object path, for the prompt. Read
// through the client so a fake BlueZ answers it in the test too; an unknown
// device (removed mid-pair) leaves them empty rather than failing the prompt.
void deviceInfo(const char* objectPath, std::string& address, std::string& name)
{
    BtState::BluetoothState state = BtClient::readState(g_agent.bus);
    for (const auto& device : state.devices) {
        if (device.objectPath == objectPath) {
            address = device.address;
            name = device.name;
            return;
        }
    }
}

// Hold a call open and tell the card. Only one at a time: a second request
// while one is pending is rejected (BlueZ should not do this, but a rejected
// call is better than losing the first one's invocation pointer). The
// invocation is always a real one here -- the display-only prompts, which have
// no reply to hold, go through pushDisplay instead and never reach this slot.
void beginPrompt(GDBusMethodInvocation* invocation, PromptKind kind,
                 const char* devicePath, const std::string& passkey,
                 const std::string& pinCode)
{
    if (g_agent.pending) {
        // Reject the latecomer rather than overwrite (and leak) the pending
        // one. invocation is non-null on every path that reaches here.
        g_dbus_method_invocation_return_dbus_error(
            invocation, "org.bluez.Error.Rejected", "another pairing is in progress");
        return;
    }
    Prompt prompt;
    prompt.kind = kind;
    prompt.passkey = passkey;
    prompt.pinCode = pinCode;
    deviceInfo(devicePath, prompt.deviceAddress, prompt.deviceName);

    g_agent.pending = invocation;
    g_agent.pendingKind = kind;
    if (g_agent.onPrompt)
        g_agent.onPrompt(prompt);
}

// A display-only prompt (DisplayPasskey/DisplayPinCode): BlueZ wants no reply
// value and does not wait on the card, so the invocation is already returned by
// the caller and nothing is held in the pending slot. This only tells the card
// what to show, and it never touches g_agent.pending -- so a display update
// arriving while a Request* is pending cannot disturb it, and there is no null
// invocation to dereference.
void pushDisplay(PromptKind kind, const char* devicePath,
                 const std::string& passkey, const std::string& pinCode)
{
    Prompt prompt;
    prompt.kind = kind;
    prompt.passkey = passkey;
    prompt.pinCode = pinCode;
    deviceInfo(devicePath, prompt.deviceAddress, prompt.deviceName);
    if (g_agent.onPrompt)
        g_agent.onPrompt(prompt);
}

// Methods with no better answer than yes: an incoming authorization, or a
// service authorization, from a settings UI that has no policy to say no. BlueZ
// still gates them behind the device being paired.
void autoAccept(GDBusMethodInvocation* invocation)
{
    g_dbus_method_invocation_return_value(invocation, nullptr);
}

void handleMethod(GDBusConnection*, const gchar*, const gchar*, const gchar*,
                  const gchar* method, GVariant* parameters,
                  GDBusMethodInvocation* invocation, gpointer)
{
    if (g_strcmp0(method, "Release") == 0 || g_strcmp0(method, "Cancel") == 0) {
        // Cancel: BlueZ aborted the pairing. Drop the pending call -- it belongs
        // to this pairing and BlueZ will not read its reply any more.
        if (g_strcmp0(method, "Cancel") == 0 && g_agent.pending) {
            g_dbus_method_invocation_return_dbus_error(
                g_agent.pending, "org.bluez.Error.Canceled", "cancelled");
            g_agent.pending = nullptr;
        }
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }

    const char* devicePath = nullptr;

    if (g_strcmp0(method, "RequestPinCode") == 0) {
        g_variant_get(parameters, "(&o)", &devicePath);
        beginPrompt(invocation, PromptKind::kRequestPinCode, devicePath, "", "");
    } else if (g_strcmp0(method, "RequestPasskey") == 0) {
        g_variant_get(parameters, "(&o)", &devicePath);
        beginPrompt(invocation, PromptKind::kRequestPasskey, devicePath, "", "");
    } else if (g_strcmp0(method, "DisplayPinCode") == 0) {
        const char* pin = nullptr;
        g_variant_get(parameters, "(&o&s)", &devicePath, &pin);
        // Display methods expect no reply value; return at once and only show it.
        g_dbus_method_invocation_return_value(invocation, nullptr);
        pushDisplay(PromptKind::kDisplayPinCode, devicePath, "", pin ? pin : "");
    } else if (g_strcmp0(method, "DisplayPasskey") == 0) {
        guint32 passkey = 0;
        guint16 entered = 0;
        g_variant_get(parameters, "(&ouq)", &devicePath, &passkey, &entered);
        g_dbus_method_invocation_return_value(invocation, nullptr);
        pushDisplay(PromptKind::kDisplayPasskey, devicePath, passkeyString(passkey), "");
    } else if (g_strcmp0(method, "RequestConfirmation") == 0) {
        guint32 passkey = 0;
        g_variant_get(parameters, "(&ou)", &devicePath, &passkey);
        beginPrompt(invocation, PromptKind::kRequestConfirmation, devicePath,
                    passkeyString(passkey), "");
    } else if (g_strcmp0(method, "RequestAuthorization") == 0) {
        autoAccept(invocation);
    } else if (g_strcmp0(method, "AuthorizeService") == 0) {
        autoAccept(invocation);
    } else {
        g_dbus_method_invocation_return_dbus_error(
            invocation, "org.bluez.Error.Rejected", "unknown method");
    }
}

const GDBusInterfaceVTable kVTable = { handleMethod, nullptr, nullptr, { nullptr } };

// Register (and make default) with AgentManager1. Separate from exporting the
// object so a failure to register still leaves the object unexported cleanly.
bool registerWithBluez(GDBusConnection* bus, std::string& error)
{
    GError* gerror = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        bus, kBluez, "/org/bluez", kAgentManagerIface, "RegisterAgent",
        g_variant_new("(os)", kAgentPath, kCapability), nullptr,
        G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &gerror);
    if (!reply) {
        error = gerror && gerror->message ? gerror->message : "RegisterAgent failed";
        g_clear_error(&gerror);
        return false;
    }
    g_variant_unref(reply);

    reply = g_dbus_connection_call_sync(
        bus, kBluez, "/org/bluez", kAgentManagerIface, "RequestDefaultAgent",
        g_variant_new("(o)", kAgentPath), nullptr,
        G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &gerror);
    if (!reply) {
        // Not fatal: being registered but not default still works when no other
        // agent is around, which on this machine is the case.
        g_warning("bluetooth: RequestDefaultAgent: %s",
                  gerror && gerror->message ? gerror->message : "(no message)");
        g_clear_error(&gerror);
        return true;
    }
    g_variant_unref(reply);
    return true;
}

} // namespace

bool start(GDBusConnection* bus, OnPrompt onPrompt)
{
    if (!bus)
        return false;

    g_agent.bus = bus;
    g_agent.onPrompt = std::move(onPrompt);

    GError* gerror = nullptr;
    GDBusNodeInfo* node = g_dbus_node_info_new_for_xml(kIntrospection, &gerror);
    if (!node) {
        g_warning("bluetooth: agent introspection: %s",
                  gerror ? gerror->message : "(no message)");
        g_clear_error(&gerror);
        return false;
    }

    g_agent.registrationId = g_dbus_connection_register_object(
        bus, kAgentPath, node->interfaces[0], &kVTable, nullptr, nullptr, &gerror);
    g_dbus_node_info_unref(node);
    if (g_agent.registrationId == 0) {
        g_warning("bluetooth: register agent object: %s",
                  gerror ? gerror->message : "(no message)");
        g_clear_error(&gerror);
        return false;
    }

    std::string error;
    if (!registerWithBluez(bus, error)) {
        g_warning("bluetooth: RegisterAgent: %s", error.c_str());
        g_dbus_connection_unregister_object(bus, g_agent.registrationId);
        g_agent.registrationId = 0;
        return false;
    }
    return true;
}

void stop(GDBusConnection* bus)
{
    if (!bus || g_agent.registrationId == 0)
        return;
    GError* gerror = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        bus, kBluez, "/org/bluez", kAgentManagerIface, "UnregisterAgent",
        g_variant_new("(o)", kAgentPath), nullptr,
        G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &gerror);
    if (reply)
        g_variant_unref(reply);
    g_clear_error(&gerror);
    g_dbus_connection_unregister_object(bus, g_agent.registrationId);
    g_agent.registrationId = 0;
}

bool resolveConfirmation(bool accept)
{
    if (!g_agent.pending || g_agent.pendingKind != PromptKind::kRequestConfirmation)
        return false;
    if (accept)
        g_dbus_method_invocation_return_value(g_agent.pending, nullptr);
    else
        g_dbus_method_invocation_return_dbus_error(
            g_agent.pending, "org.bluez.Error.Rejected", "rejected by user");
    g_agent.pending = nullptr;
    return true;
}

bool resolvePasskey(const std::string& passkey)
{
    if (!g_agent.pending || g_agent.pendingKind != PromptKind::kRequestPasskey)
        return false;
    // BlueZ wants the passkey as a uint32; a non-numeric or out-of-range answer
    // is a rejection. A Bluetooth passkey is six decimal digits (0..999999), so
    // anything with a sign, trailing junk, or beyond that range is not one.
    errno = 0;
    char* end = nullptr;
    const unsigned long value = std::strtoul(passkey.c_str(), &end, 10);
    const bool valid = !passkey.empty()
        && passkey[0] != '-' && passkey[0] != '+'
        && end != passkey.c_str() && *end == '\0'
        && errno == 0
        && value <= 999999UL;
    if (!valid) {
        g_dbus_method_invocation_return_dbus_error(
            g_agent.pending, "org.bluez.Error.Rejected", "not a passkey");
    } else {
        g_dbus_method_invocation_return_value(
            g_agent.pending, g_variant_new("(u)", static_cast<guint32>(value)));
    }
    g_agent.pending = nullptr;
    return true;
}

bool resolvePinCode(const std::string& pinCode)
{
    if (!g_agent.pending || g_agent.pendingKind != PromptKind::kRequestPinCode)
        return false;
    g_dbus_method_invocation_return_value(
        g_agent.pending, g_variant_new("(s)", pinCode.c_str()));
    g_agent.pending = nullptr;
    return true;
}

void cancel()
{
    if (!g_agent.pending)
        return;
    g_dbus_method_invocation_return_dbus_error(
        g_agent.pending, "org.bluez.Error.Canceled", "cancelled");
    g_agent.pending = nullptr;
}

} // namespace BtAgent
