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

#ifndef KEYMANAGER_SECRET_KEY_STORE_H
#define KEYMANAGER_SECRET_KEY_STORE_H

//
// What com.palm.keymanager promises its callers, and how a keyname maps onto an
// item in the host's Secret Service -- both of them free of D-Bus and of the
// Luna bus on purpose, so tests/key-store.cpp can check every decision without a
// Secret Service daemon and without ls-hubd. secret_client.cpp is the other
// half: what is said over GDBus to org.freedesktop.secrets to carry these
// decisions out.
//
// On a device this was com.palm.keymanager, a native service over an encrypted
// store unlocked by the lock-screen passcode. Nothing in the CE drop provides
// it, so three callers have been talking to a service that is not there:
//
//   LunaSysMgr's Security.cpp   initialize {password}, changePassword
//                               {oldPassword,newPassword} -- the lock screen,
//                               fire-and-forget, and a registerServerStatus
//                               subscription that re-runs initialize whenever
//                               keymanager (re)appears, so the name MUST be
//                               registered for real.
//   the accounts service        fetchKey/store/remove/keyInfo {keyname,...} --
//                               the KeyStore the whole Synergy credential stack
//                               (put/get/del/has) sits on.
//   enyo's PalmServices alias    crypto -> palm://com.palm.keymanager (no live
//                               method caller in the tree).
//
// The store is the host's Secret Service (org.freedesktop.secrets): one item per
// keyname, the key's blob kept as the item's secret, found again by two
// attributes. keyInfo and fetchKey turn on whether that item exists; a missing
// key is a failure the accounts KeyStore catches to mean "not found", so the
// payloads below say returnValue:false for it rather than inventing an empty
// blob.
//

#include <cstdio>
#include <string>

namespace KeyStore {

// The Secret Service schema every item this service writes is tagged with, so a
// GNOME keyring full of a browser's and a mail client's own secrets can be told
// apart from ours and ours alone are matched, listed and removed. It is the
// value of the item's "xdg:schema" attribute, the convention libsecret and
// every Secret Service client follow.
inline const char* schema() { return "com.gachlab.webos.keymanager"; }

// The attribute a keyname is stored under. com.palm.keymanager's namespace is
// flat -- the accounts service passes an accountId as the keyname and nothing
// else -- so one attribute identifies an item within our schema.
inline const char* keynameAttribute() { return "keyname"; }

// --- requests ---------------------------------------------------------------
// Each parsed out of the request's JSON by main.cpp; kept here as plain structs
// so the validation that decides whether a request is answerable at all can be
// checked without the bus.

struct StoreRequest {
    std::string keyname;
    std::string keydata; // the blob the caller wants kept; opaque to us
    std::string type;    // HP's "ASCIIBLOB" etc. -- carried, not interpreted
    bool nohide = false; // HP's flag; the host keyring has no "hidden" notion
};

// A keyname is required by every method that names one. HP's service keyed its
// store on it, and an empty one is not a key -- it is the whole store, which is
// how enyo's deleteprofile-with-no-id bug deleted everything on the phone.
inline bool validKeyname(const std::string& keyname)
{
    return !keyname.empty();
}

// A store needs a keyname and a blob to put under it. Everything else HP's
// callers send (type, nohide) is accepted and carried but never gates the call:
// the host keyring stores an opaque secret, and refusing one for its declared
// type would reject credentials the accounts service depends on keeping.
inline bool validStore(const StoreRequest& request)
{
    return validKeyname(request.keyname) && !request.keydata.empty();
}

// --- replies ----------------------------------------------------------------
// Built here, as strings, for the same reason power_state.h builds its payloads
// here: the exact shape is the contract, and the accounts KeyStore reads named
// fields out of it (keydata on fetch) and treats a thrown future as "not found".

// JSON string escaping, for a keyname or a message that reaches a payload. A
// keydata blob is a caller-supplied string (the accounts service hands us
// JSON.stringify output) and rides through the same escape; without it a quote
// in the blob would break the reply the accounts service then JSON.parses.
inline std::string jsonEscape(const std::string& in)
{
    std::string out;
    out.reserve(in.size() + 2);
    for (char c : in) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

inline std::string okPayload()
{
    return "{\"returnValue\":true}";
}

inline std::string errorPayload(const std::string& message)
{
    return std::string("{\"returnValue\":false,\"errorText\":\"") + jsonEscape(message) + "\"}";
}

// fetchKey's reply. The accounts service's getCredentials does
// JSON.parse(result.keydata), so the blob is returned verbatim under "keydata",
// escaped as a JSON string value.
inline std::string fetchPayload(const std::string& keydata)
{
    return std::string("{\"returnValue\":true,\"keydata\":\"") + jsonEscape(keydata) + "\"}";
}

// keyInfo's reply when the key exists. The accounts service's hasCredentials
// reads nothing out of it beyond the call not throwing, so success is the whole
// message; a missing key is errorPayload instead, which its future turns into
// has()=false.
inline std::string keyInfoPayload()
{
    return "{\"returnValue\":true}";
}

// The one error text that is a contract and not a diagnostic: a fetch or an
// info for a key that is not stored. getCredentials and hasCredentials both key
// off the call failing, not off this text, but it is the honest message and the
// one the log will show.
inline const char* notFoundMessage() { return "key not found"; }

} // namespace KeyStore

#endif // KEYMANAGER_SECRET_KEY_STORE_H
