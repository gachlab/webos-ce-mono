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
// The pairing agent: an org.bluez.Agent1 the service exports and registers with
// BlueZ's AgentManager1, so pairing a device goes through our UI rather than
// bluetoothctl's terminal prompt.
//
// HP's app had no such thing to port -- its pairing was legacy PIN/passkey
// handled inside the device-only service. BlueZ 5's Secure Simple Pairing is
// richer: a headset is Just Works, a keyboard displays a passkey to type, a
// phone shows the same six digits on both screens to compare. The capability we
// register, "KeyboardDisplay", is the one that lets BlueZ pick any of these.
//
// How a prompt reaches the user: when BlueZ calls one of the Agent1 methods
// below, the call is left pending on D-Bus and the request is pushed to the card
// through the AgentPrompt callback (main.cpp posts it on
// com.palm.bluetooth/gap). The card answers through a bus method
// (gap/supplyconfirmation, gap/supplypasskey, gap/supplypincode), which calls
// resolve* here to send BlueZ its reply. A method with no UI answer -- Just
// Works' RequestConfirmation when the card is not up, AuthorizeService -- is
// accepted automatically, as a settings UI with no better information must.
//
// The agent takes the D-Bus connection it exports on, so it is testable against
// a fake BlueZ on a private bus like the rest of bluez_client.
//

#ifndef WEBOS_BLUETOOTH_BLUEZ_AGENT_H
#define WEBOS_BLUETOOTH_BLUEZ_AGENT_H

#include <gio/gio.h>

#include <functional>
#include <string>

namespace BtAgent {

// What the user is being asked. The card renders one screen per kind.
enum class PromptKind {
    kRequestPinCode,       // legacy: type a PIN string (keyboards, old devices)
    kRequestPasskey,       // type a numeric passkey
    kDisplayPinCode,       // show a PIN for the user to enter on the device
    kDisplayPasskey,       // show a passkey the user enters on the device
    kRequestConfirmation,  // compare six digits, yes/no
    kRequestAuthorization, // authorize an incoming pairing, yes/no
};

// A prompt headed for the card. passkey is set for the passkey/confirmation
// kinds; pinCode for the PIN kinds; both empty when the kind does not carry one.
struct Prompt {
    PromptKind kind;
    std::string deviceAddress;
    std::string deviceName;
    std::string passkey;   // six digits, as a string, for display/confirmation
    std::string pinCode;   // for DisplayPinCode
};

// Pushed when BlueZ asks something that needs the user. The service turns it
// into a subscription post; see main.cpp.
using OnPrompt = std::function<void(const Prompt&)>;

// Exports org.bluez.Agent1 on bus and registers it as BlueZ's default agent.
// Returns false (with a g_warning) if the export or the registration fails;
// the service still runs, and pairing then falls back to BlueZ's own default,
// which is no agent -- Just Works pairings still succeed, interactive ones are
// rejected rather than hanging.
bool start(GDBusConnection* bus, OnPrompt onPrompt);

// Unregisters and unexports the agent. Safe to call when start failed.
void stop(GDBusConnection* bus);

// The card's answers, routed back to the pending BlueZ call. Each returns false
// when there is no call of that kind outstanding (a stale or duplicate answer),
// which the bus method reports as returnValue:false.
bool resolveConfirmation(bool accept);
bool resolvePasskey(const std::string& passkey);
bool resolvePinCode(const std::string& pinCode);

// Rejects whatever is pending (the card dismissed the prompt, or pairing was
// cancelled). No-op when nothing is pending.
void cancel();

} // namespace BtAgent

#endif
