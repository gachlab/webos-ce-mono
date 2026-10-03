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
// Everything this service says to BlueZ, and nothing it says to webOS.
//
// Every call takes the D-Bus connection it should use rather than reaching for
// the system bus itself. main.cpp hands it the system bus; tests/bluez-client.cpp
// hands it a private bus with a fake BlueZ (python-dbusmock's bluez5 template)
// on it, so what is read from BlueZ and what is asked of it are checked without
// the host's adapter and without ls-hubd. bluez_state.h is the other half: what
// the state read here becomes on the webOS bus.
//
// The read is one GetManagedObjects on BlueZ's ObjectManager, which returns the
// adapter and every device in one round trip -- the shape nm_client's readState
// has for NetworkManager, but BlueZ hands the whole tree at once rather than a
// device list to walk.
//

#ifndef WEBOS_BLUETOOTH_BLUEZ_CLIENT_H
#define WEBOS_BLUETOOTH_BLUEZ_CLIENT_H

#include "bluez_state.h"

#include <gio/gio.h>

#include <string>

namespace BtClient {

// The adapter and its devices as BlueZ reports them. A null connection, or one
// where BlueZ does not answer, reads as a machine with no adapter -- a radio
// that is off and cannot be turned on, which is the truth as far as this can
// tell.
BtState::BluetoothState readState(GDBusConnection* bus);

// The object path of the first adapter (/org/bluez/hciN), or "" when there is
// none. Several calls below need it, and it is read once per call rather than
// cached, since an adapter can be plugged or pulled (a USB dongle) at any time.
std::string firstAdapterPath(GDBusConnection* bus);

// The object path of the device with this address under the first adapter, or
// "" when BlueZ has no object for it. profconnect/profdisconnect and pairing all
// start from an address (what webOS speaks) and need the path (what BlueZ does).
std::string devicePathForAddress(GDBusConnection* bus, const std::string& address);

// --- btmonitor: the radio ---------------------------------------------------

// Switches the adapter's Powered. On turn-on, Pairable/Connectable follow the
// {"visible","connectable"} the caller passed. error holds BlueZ's own message,
// or "no bluetooth adapter" on a machine without one.
bool setPowered(GDBusConnection* bus, bool on, bool visible, bool connectable,
                std::string& error);

// --- bluetooth gap: discovery -----------------------------------------------

// StartDiscovery / StopDiscovery on the adapter, with an optional transport
// filter ("le" | "bredr" | "auto") set first through SetDiscoveryFilter. HP had
// no filter; the card uses it to scan LE-only or classic-only.
bool startDiscovery(GDBusConnection* bus, const std::string& transport, std::string& error);
bool stopDiscovery(GDBusConnection* bus, std::string& error);

// --- bluetooth gap: pairing -------------------------------------------------

// Device1.Pair / CancelPairing / RemoveDevice. Pairing drives the Agent1 the
// service registers (main.cpp) for PIN, passkey and numeric comparison; this
// just starts and cancels it. RemoveDevice is "forget".
bool pairDevice(GDBusConnection* bus, const std::string& address, std::string& error);
bool cancelPairing(GDBusConnection* bus, const std::string& address, std::string& error);
bool removeDevice(GDBusConnection* bus, const std::string& address, std::string& error);

// Device1.Trusted, the writable property gettrusteddevices filters on.
bool setTrusted(GDBusConnection* bus, const std::string& address, bool trusted,
                std::string& error);

// --- bluetooth prof: connect / disconnect -----------------------------------

// profconnect / profdisconnect. A profile name (HP's hfg/a2dp/...) connects the
// matching UUID through Device1.ConnectProfile; "all", or a device with no UUID
// for the profile, falls back to Device1.Connect/Disconnect, which is the whole
// device. error holds BlueZ's message.
bool connectProfile(GDBusConnection* bus, const std::string& address,
                    const std::string& profile, std::string& error);
bool disconnectProfile(GDBusConnection* bus, const std::string& address,
                       const std::string& profile, std::string& error);

} // namespace BtClient

#endif
