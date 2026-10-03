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
// Everything com.palm.zeroconf says to Avahi, and nothing it says to webOS.
//
// Every call takes the D-Bus connection it should use rather than reaching for
// the system bus itself. main.cpp hands it the system bus (where
// org.freedesktop.Avahi lives); tests/avahi-client.cpp hands it a private bus
// with a fake Avahi on it, so what is read and asked is checked without the
// host's mDNS daemon and without ls-hubd. zeroconf_records.h is the other half:
// the vocabulary and the payloads these results become on the webOS bus.
//
// Browse and register are stateful on Avahi's side: ServiceBrowserNew and
// EntryGroupNew each return an object path for an object the daemon owns and
// drives with signals (ItemNew/ItemRemove/AllForNow for a browser, StateChanged
// for a group). Those live objects are what a subscription holds open, so they
// are returned to the caller (main.cpp) as handles to free when the subscriber
// goes. resolve is the one synchronous call -- ResolveService blocks and
// returns the host, address, port and TXT in one round trip.
//

#ifndef ZEROCONF_AVAHI_AVAHI_CLIENT_H
#define ZEROCONF_AVAHI_AVAHI_CLIENT_H

#include "zeroconf_records.h"

#include <gio/gio.h>

#include <functional>
#include <string>
#include <vector>

namespace AvahiClient {

// A live browser: the object path Avahi handed back, and the subscriptions on
// its signals. Freeing it stops the browse and drops the daemon's object.
struct Browser {
    GDBusConnection* bus = nullptr;
    std::string path;
    guint itemNew = 0;
    guint itemRemove = 0;
    guint allForNow = 0;
    guint failure = 0;
};

// Called on each browser event: the full current set, and what moved
// ("added" / "removed" / "complete"). "complete" is Avahi's AllForNow -- the
// first pass of the cache is done -- which is when a one-shot browse replies.
using BrowseCallback = std::function<void(const std::vector<Zeroconf::Service>&, const char* changed)>;

// Starts a browse for a DNS-SD type on all interfaces and the default domain.
// On success browser holds the live object and callback fires as services come
// and go; on failure the return is false and error carries Avahi's message, or
// a short reason when Avahi is not there. The caller owns browser and must
// stopBrowse it. The request is expected to have passed validServiceType.
bool startBrowse(GDBusConnection* bus, const std::string& type,
                 BrowseCallback callback, Browser& browser, std::string& error);
void stopBrowse(Browser& browser);

// Resolves one instance synchronously. On a name that does not resolve (gone
// between browse and resolve, or never there), the return is false and error
// says so; the service turns that into a failed reply.
bool resolve(GDBusConnection* bus, const std::string& name, const std::string& type,
             const std::string& domain, Zeroconf::Resolved& resolved, std::string& error);

// A published service: the entry-group object path that holds it alive. Freeing
// it withdraws the service from the network.
struct Entry {
    GDBusConnection* bus = nullptr;
    std::string path;
};

// Publishes a service: EntryGroupNew, AddService, Commit. txt is the DNS-SD TXT
// as key/value pairs, rendered to Avahi's array-of-byte-strings form. On
// success entry holds the live group; freeing it (unregister) withdraws it. The
// request is expected to have passed validServiceType / validName / validPort.
bool registerService(GDBusConnection* bus, const Zeroconf::Service& service, int port,
                     const std::vector<std::pair<std::string, std::string>>& txt,
                     Entry& entry, std::string& error);
void unregisterService(Entry& entry);

} // namespace AvahiClient

#endif // ZEROCONF_AVAHI_AVAHI_CLIENT_H
