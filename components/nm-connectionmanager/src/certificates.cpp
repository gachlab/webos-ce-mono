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

#include "certificates.h"

#include <gio/gio.h>

#include <algorithm>

namespace Certificates {

std::string directory()
{
    const char* configured = g_getenv("WEBOS_CERTIFICATE_DIR");
    if (configured && *configured)
        return configured;
    gchar* path = g_build_filename(g_get_user_data_dir(), "webos-ce", "certificates", nullptr);
    std::string out = path;
    g_free(path);
    return out;
}

std::vector<NmNet::Certificate> list(const std::string& dir)
{
    std::vector<std::string> names;
    if (GDir* handle = g_dir_open(dir.c_str(), 0, nullptr)) {
        while (const gchar* name = g_dir_read_name(handle)) {
            const std::string n = name;
            if (g_str_has_suffix(name, ".pem") || g_str_has_suffix(name, ".crt"))
                names.push_back(n);
        }
        g_dir_close(handle);
    }
    std::sort(names.begin(), names.end());

    std::vector<NmNet::Certificate> out;
    for (const std::string& name : names) {
        gchar* path = g_build_filename(dir.c_str(), name.c_str(), nullptr);
        GError* error = nullptr;
        GTlsCertificate* cert = g_tls_certificate_new_from_file(path, &error);
        if (cert) {
            gchar* subject = nullptr;
            g_object_get(cert, "subject-name", &subject, nullptr);
            NmNet::Certificate entry;
            entry.certificateId = static_cast<int>(out.size()) + 1;
            entry.path = path;
            if (subject) {
                entry.commonName = NmNet::dnField(subject, "CN");
                entry.organization = NmNet::dnField(subject, "O");
                g_free(subject);
            }
            if (entry.commonName.empty() && entry.organization.empty())
                entry.commonName = name;
            out.push_back(entry);
            g_object_unref(cert);
        } else {
            g_clear_error(&error);
        }
        g_free(path);
    }
    return out;
}

} // namespace Certificates
