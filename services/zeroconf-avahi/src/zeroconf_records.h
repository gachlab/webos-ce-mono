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

#ifndef ZEROCONF_AVAHI_ZEROCONF_RECORDS_H
#define ZEROCONF_AVAHI_ZEROCONF_RECORDS_H

//
// com.palm.zeroconf's vocabulary and payloads, free of D-Bus and of the Luna
// bus, so tests/zeroconf-records.cpp can check every decision without an Avahi
// daemon and without ls-hubd. avahi_client.cpp is the other half: what is said
// over GDBus to org.freedesktop.Avahi to carry these out.
//
// A WORD ON WHERE THIS API COMES FROM. Unlike the other services in this tree,
// the shape here is NOT taken from a surviving HP caller. HP's com.palm.zeroconf
// shipped only on the device and was never released, and nothing in this tree
// invokes a method on it -- enyo's framework only carries the URI alias
// (PalmServices.js: zeroconf -> palm://com.palm.zeroconf/) with no consumer.
// There is therefore no HP specification to mirror, which is why this was split
// out of #43's first pass. The method and payload names below are the DNS-SD
// vocabulary every mDNS library converges on -- browse, resolve, register --
// paired with this tree's own subscription idiom (as com.palm.btmonitor's
// subscribenotifications has). They are PROVISIONAL: the first real caller, or
// a chosen API, gets to rename them, and this header is the one place that
// changes when it does.
//
// The store of truth is Avahi (org.freedesktop.Avahi), the host's mDNS/DNS-SD
// daemon, reached over GDBus the same way nm-connectionmanager reaches
// NetworkManager and services/bluetooth reaches BlueZ -- no new dependency, and
// no bundled mDNS stack.
//

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace Zeroconf {

// One discovered service instance, as a browse turns it up: the instance name
// ("Office Printer"), the DNS-SD type ("_ipp._tcp"), and the domain ("local").
// Browse stops here; resolve is what adds the host, address, port and TXT.
struct Service {
    std::string name;
    std::string type;
    std::string domain;

    bool operator==(const Service& o) const
    {
        return name == o.name && type == o.type && domain == o.domain;
    }
};

// A resolved instance: everything a client needs to actually connect.
struct Resolved {
    std::string name;
    std::string type;
    std::string domain;
    std::string hostname;              // "printer.local"
    std::string address;               // "192.168.1.20"
    int port = 0;
    std::map<std::string, std::string> txt; // DNS-SD TXT key/value pairs
};

// A DNS-SD service type is "_service._proto", proto being _tcp or _udp. The
// request names one to browse or register under; a malformed one is a request
// error, not an empty result, so the caller hears it as such rather than as "no
// services found". Avahi would reject it too, but catching it here keeps the
// error message ours and the round trip saved.
inline bool validServiceType(const std::string& type)
{
    const std::string::size_type dot = type.rfind('.');
    if (type.empty() || type[0] != '_' || dot == std::string::npos)
        return false;
    const std::string proto = type.substr(dot + 1);
    return proto == "_tcp" || proto == "_udp";
}

inline bool validName(const std::string& name)
{
    return !name.empty();
}

// register needs a valid type, a name, and a port in range. A port of 0 is not
// a service anyone can reach, so it is refused rather than published.
inline bool validPort(int port)
{
    return port > 0 && port <= 65535;
}

// --- payloads ---------------------------------------------------------------
// Built here as strings, as power_state.h and network_state.h build theirs: the
// exact shape is the contract. The subscription idiom follows the rest of the
// tree -- a reply carries returnValue, and a pushed update is the same shape so
// a subscriber reads one parser.

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

inline std::string errorPayload(const std::string& message)
{
    return std::string("{\"returnValue\":false,\"errorText\":\"") + jsonEscape(message) + "\"}";
}

inline std::string serviceObject(const Service& s)
{
    return std::string("{\"name\":\"") + jsonEscape(s.name)
         + "\",\"type\":\"" + jsonEscape(s.type)
         + "\",\"domain\":\"" + jsonEscape(s.domain) + "\"}";
}

// A browse reply, or a pushed update. "services" is the full current set; a
// subscriber is handed the whole list on every change rather than a delta, the
// way the status bar's connectors are handed whole payloads -- simpler to read,
// and a browse list is small. "changed" names what moved ("added"/"removed"/
// "complete"), echoing btmonitor's notifn* idea so a caller can react without
// diffing.
inline std::string browsePayload(const std::vector<Service>& services, bool subscribed,
                                 const char* changed)
{
    std::string out = "{\"returnValue\":true";
    if (subscribed)
        out += ",\"subscribed\":true";
    if (changed && *changed) {
        out += ",\"changed\":\"";
        out += changed;
        out += "\"";
    }
    out += ",\"services\":[";
    for (std::size_t i = 0; i < services.size(); ++i) {
        if (i)
            out += ',';
        out += serviceObject(services[i]);
    }
    out += "]}";
    return out;
}

inline std::string resolvePayload(const Resolved& r)
{
    std::string out = "{\"returnValue\":true,\"name\":\"" + jsonEscape(r.name)
                    + "\",\"type\":\"" + jsonEscape(r.type)
                    + "\",\"domain\":\"" + jsonEscape(r.domain)
                    + "\",\"hostname\":\"" + jsonEscape(r.hostname)
                    + "\",\"address\":\"" + jsonEscape(r.address)
                    + "\",\"port\":" + std::to_string(r.port)
                    + ",\"txt\":{";
    bool first = true;
    for (const auto& kv : r.txt) {
        if (!first)
            out += ',';
        first = false;
        out += "\"" + jsonEscape(kv.first) + "\":\"" + jsonEscape(kv.second) + "\"";
    }
    out += "}}";
    return out;
}

inline std::string registeredPayload(const Service& s)
{
    return std::string("{\"returnValue\":true,\"registered\":true,\"name\":\"")
         + jsonEscape(s.name) + "\",\"type\":\"" + jsonEscape(s.type) + "\"}";
}

// Avahi carries TXT as an array of raw byte strings, each "key=value" (or a bare
// "key" with no '='). This splits one entry; a '=' with nothing before it is
// skipped, since a TXT key is never empty.
inline bool parseTxtEntry(const std::string& entry, std::string& key, std::string& value)
{
    const std::string::size_type eq = entry.find('=');
    if (eq == std::string::npos) {
        if (entry.empty())
            return false;
        key = entry;
        value.clear();
        return true;
    }
    if (eq == 0)
        return false;
    key = entry.substr(0, eq);
    value = entry.substr(eq + 1);
    return true;
}

} // namespace Zeroconf

#endif // ZEROCONF_AVAHI_ZEROCONF_RECORDS_H
