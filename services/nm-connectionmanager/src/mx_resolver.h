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
// The one DNS call behind com.palm.nettools/findMxRecords: GIO's resolver,
// asked for a domain's MX records. mx_records.h is the other half -- what the
// records become on the webOS bus, and the ordering -- and is free of DNS so it
// can be tested without one; this is the part that genuinely touches the
// network.
//
// The resolver is passed in rather than taken from g_resolver_get_default(), so
// a test can hand a resolver pointed at a fake nameserver (GResolver reads
// GLIB_DNS_* / its proxy resolver) without the build host's own DNS deciding
// whether the test passes. A null resolver means "use the default", which is
// what main.cpp does.
//

#ifndef NM_MX_RESOLVER_H
#define NM_MX_RESOLVER_H

#include "mx_records.h"

#include <gio/gio.h>

#include <string>
#include <vector>

namespace NmMx {

// Looks up the MX records for domain. On success records holds them (unsorted;
// recordsPayload orders them) and the return is true, including the true-with-
// empty-list case of a domain that resolves but publishes no MX. On failure the
// return is false and error carries the resolver's own message -- NXDOMAIN, a
// timeout, no network -- which findMxRecords turns into an errorCode the wizard
// reads as "could not look up".
bool lookup(GResolver* resolver, const std::string& domain,
            std::vector<Record>& records, std::string& error);

} // namespace NmMx

#endif // NM_MX_RESOLVER_H
