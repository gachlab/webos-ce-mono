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

#include "mx_resolver.h"

namespace NmMx {

bool lookup(GResolver* resolver, const std::string& domain,
            std::vector<Record>& records, std::string& error)
{
    records.clear();

    GResolver* owned = nullptr;
    if (!resolver) {
        owned = g_resolver_get_default();
        resolver = owned;
    }

    GError* gerror = nullptr;
    // Synchronous: findMxRecords is a one-shot request/response the wizard
    // waits on, and the service's main loop is free to answer other calls on
    // other handles while this blocks -- the lookup is its own call, not a
    // signal burst. GResolver runs it on a thread internally.
    GList* results = g_resolver_lookup_records(
        resolver, domain.c_str(), G_RESOLVER_RECORD_MX, nullptr, &gerror);

    if (gerror) {
        // No MX records is reported by GIO as G_RESOLVER_ERROR_NOT_FOUND with a
        // null list. That is not a failure of the lookup -- the domain simply
        // publishes none -- so it is an empty success, which recordsPayload
        // renders as an empty mxRecords array and the wizard reads as "nothing
        // to try here".
        const bool notFound = g_error_matches(gerror, G_RESOLVER_ERROR,
                                              G_RESOLVER_ERROR_NOT_FOUND);
        if (!notFound)
            error = (gerror->message && *gerror->message) ? gerror->message : "DNS lookup failed";
        g_clear_error(&gerror);
        if (owned)
            g_object_unref(owned);
        return notFound;
    }

    // Each record is a GVariant "(qs)": the preference (a uint16) and the mail
    // exchanger's hostname. GIO strips the DNS trailing dot already.
    for (GList* item = results; item; item = item->next) {
        GVariant* record = static_cast<GVariant*>(item->data);
        guint16 preference = 0;
        const gchar* server = nullptr;
        g_variant_get(record, "(q&s)", &preference, &server);
        if (server && *server)
            records.push_back({ server, static_cast<int>(preference) });
    }
    g_list_free_full(results, reinterpret_cast<GDestroyNotify>(g_variant_unref));

    if (owned)
        g_object_unref(owned);
    return true;
}

} // namespace NmMx
