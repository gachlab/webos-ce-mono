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
// Everything com.palm.keymanager says to the host's Secret Service, and nothing
// it says to webOS.
//
// Every call takes the D-Bus connection it should use rather than reaching for
// the session bus itself. main.cpp hands it the session bus (where
// org.freedesktop.secrets lives -- gnome-keyring, KWallet's Secret Service
// front, or KeePassXC); tests/secret-client.cpp hands it a private bus with a
// fake Secret Service on it, so what is read and written is checked without the
// user's real keyring and without ls-hubd. key_store.h is the other half: the
// schema, the attribute a keyname is kept under, and the payloads these results
// become on the webOS bus.
//
// The talk is the plain Secret Service protocol: OpenSession("plain") -- the
// value travels the session bus in the clear, which is a local socket owned by
// the user -- then SearchItems/CreateItem/GetSecret/Delete on the default
// collection. No libsecret: gio's D-Bus client is already linked for the main
// loop, as it is in nm-connectionmanager, and libsecret is not present on the
// build host anyway.
//

#ifndef KEYMANAGER_SECRET_SECRET_CLIENT_H
#define KEYMANAGER_SECRET_SECRET_CLIENT_H

#include "key_store.h"

#include <gio/gio.h>

#include <string>

namespace SecretClient {

// A plain session with the Secret Service, held for the life of a call sequence.
// It owns the session object path the service handed back; closing it is best
// effort, since the service drops it when our connection goes anyway.
struct Session {
    GDBusConnection* bus = nullptr;
    std::string path;    // the /org/freedesktop/secrets/session/* object
    bool open = false;
};

// Opens a plain session and finds the default collection, unlocking it if the
// service will do so without prompting. On failure error holds the service's
// own message, or a short reason when the service is simply not there -- which
// on a headless build host it will not be, and the test provides instead.
bool openSession(GDBusConnection* bus, Session& session, std::string& error);
void closeSession(Session& session);

// --- initialize / changePassword --------------------------------------------
// HP's lock screen drove these: the passcode unlocked the keystore. The host
// keyring is unlocked by the user's own login (PAM) or by its own prompt, not
// by webOS's passcode, so these do not set the keyring's password -- doing so
// would lock the user out of secrets this service does not own. They make sure
// the default collection is present and try to unlock it, which is the part
// that is ours to do, and report success so Security.cpp's retry loop settles.

bool initialize(GDBusConnection* bus, std::string& error);
bool changePassword(GDBusConnection* bus, std::string& error);

// --- the KeyStore the accounts service uses ---------------------------------
// Each keyed on key_store.h's keyname attribute within our schema, so nothing
// outside what this service wrote is ever read, replaced or deleted.

// Writes the blob under keyname, replacing any item this service already keeps
// for it. CreateItem with replace=true does the replace atomically.
bool store(GDBusConnection* bus, const KeyStore::StoreRequest& request, std::string& error);

// The blob stored under keyname. found says whether an item existed; when it
// did not, keydata is untouched and error carries key_store.h's not-found
// message, which fetchKey turns into a failed reply the accounts service reads
// as "no credentials".
bool fetch(GDBusConnection* bus, const std::string& keyname, std::string& keydata,
           bool& found, std::string& error);

// Removes the item for keyname. A keyname with no item is not an error: the
// accounts service's putCredentials removes before every store, including the
// first, when nothing is there yet.
bool remove(GDBusConnection* bus, const std::string& keyname, std::string& error);

// Whether an item exists for keyname, for keyInfo. A service error (the keyring
// gone, a locked collection that will not open) is distinct from a clean "no":
// error is set only for the former, so keyInfo can fail loudly on a broken
// store and quietly on an absent key.
bool exists(GDBusConnection* bus, const std::string& keyname, bool& present, std::string& error);

} // namespace SecretClient

#endif // KEYMANAGER_SECRET_SECRET_CLIENT_H
