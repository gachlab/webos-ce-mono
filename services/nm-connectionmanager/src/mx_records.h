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

#ifndef NM_MX_RECORDS_H
#define NM_MX_RECORDS_H

//
// com.palm.nettools/findMxRecords, free of DNS and of the Luna bus, so
// tests/mx-records.cpp can check the payload the Email app reads without a
// resolver and without ls-hubd. mx_resolver.cpp is the other half: the one
// GResolver call that fills the list in.
//
// On a device nettools was a native service; the one method anything in this
// tree calls is findMxRecords, from the Email account wizard
// (AccountWizard.js). It hands a domain and reads back the mail exchangers so it
// can guess a server to try. Its loop keeps the record with the lowest
// mxPreference, so the field names and the number are the contract:
//
//   request   {"domainName": "example.com"}
//   reply     {"returnValue": true,
//              "mxRecords": [{"mxServer": "mx1.example.com", "mxPreference": 10},
//                            ...]}
//
// The list is returned already ordered by preference (lowest first), which is
// what a mail client would try in turn; the wizard only reads the best, but an
// ordered list is the honest answer and costs nothing.
//

#include "network_state.h" // NmNet::jsonEscape, NmNet::errorPayload

#include <algorithm>
#include <string>
#include <vector>

namespace NmMx {

// One mail exchanger, as DNS gives it: a hostname and its preference (lower is
// tried first). The weight and the trailing dot DNS carries are not part of
// what the wizard reads, so they are dropped where the record is read.
struct Record {
    std::string server;
    int preference = 0;
};

// A sane domain to look up. The resolver would reject nonsense anyway, but an
// empty or whitespace domain is a request error the wizard should hear as one,
// not a DNS miss. No attempt to validate the name beyond that: the resolver is
// the authority on what resolves.
inline bool validDomain(const std::string& domain)
{
    if (domain.empty())
        return false;
    for (char c : domain)
        if (c != ' ' && c != '\t')
            return true;
    return false;
}

// By preference, lowest first; ties keep the order DNS returned them in, which
// is already randomised by the resolver for load spreading, so stable_sort
// preserves that spread rather than reordering equal-preference hosts.
inline void sortByPreference(std::vector<Record>& records)
{
    std::stable_sort(records.begin(), records.end(),
                     [](const Record& a, const Record& b) { return a.preference < b.preference; });
}

// The reply the wizard reads. records are sorted here, so a caller need not;
// the server names are escaped because a hostname is attacker-influenced (it
// comes from the queried domain's DNS) and a quote or backslash in one would
// otherwise break the JSON the wizard parses.
inline std::string recordsPayload(std::vector<Record> records)
{
    sortByPreference(records);
    std::string out = "{\"returnValue\":true,\"mxRecords\":[";
    for (size_t i = 0; i < records.size(); ++i) {
        if (i)
            out += ',';
        out += "{\"mxServer\":\"";
        out += NmNet::jsonEscape(records[i].server);
        out += "\",\"mxPreference\":";
        out += std::to_string(records[i].preference);
        out += '}';
    }
    out += "]}";
    return out;
}

} // namespace NmMx

#endif // NM_MX_RECORDS_H
