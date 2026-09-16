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

namespace NmClient {

// The machine's network as NetworkManager reports it. A null connection, or one
// where NetworkManager does not answer, reads as a disconnected machine -- which
// is the truth as far as anything here can tell.
NmNet::NetworkState readState(GDBusConnection* bus);

// Connects or disconnects the cable. On failure, error holds NetworkManager's
// own message, or "no wired device" when the machine has no ethernet socket.
bool setWired(GDBusConnection* bus, bool connected, std::string& error);

} // namespace NmClient

#endif
