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
#include <glib/gstdio.h>

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

static bool ensureDir(const std::string& dir, std::string& error)
{
    if (g_file_test(dir.c_str(), G_FILE_TEST_IS_DIR))
        return true;
    if (g_mkdir_with_parents(dir.c_str(), 0700) != 0) {
        error = "could not create the certificate store";
        return false;
    }
    return true;
}

static std::string uniqueName(const std::string& dir, const std::string& base)
{
    std::string leaf = base.empty() ? "certificate.pem" : base;
    if (!g_str_has_suffix(leaf.c_str(), ".pem") && !g_str_has_suffix(leaf.c_str(), ".crt"))
        leaf += ".pem";
    gchar* candidate = g_build_filename(dir.c_str(), leaf.c_str(), nullptr);
    if (!g_file_test(candidate, G_FILE_TEST_EXISTS)) {
        std::string out = leaf;
        g_free(candidate);
        return out;
    }
    g_free(candidate);
    for (int n = 1; n < 1000; ++n) {
        gchar* numbered = g_strdup_printf("%d-%s", n, leaf.c_str());
        gchar* path = g_build_filename(dir.c_str(), numbered, nullptr);
        const bool free = !g_file_test(path, G_FILE_TEST_EXISTS);
        std::string name = numbered;
        g_free(path);
        g_free(numbered);
        if (free)
            return name;
    }
    return leaf;
}

bool add(const std::string& dir, const std::string& sourcePath,
         const std::string& passphrase, NmNet::Certificate& out, std::string& error)
{
    if (sourcePath.empty()) {
        error = "certificateFilename is required";
        return false;
    }
    if (!passphrase.empty()) {
        error = "encrypted certificates are not supported";
        return false;
    }
    if (!g_file_test(sourcePath.c_str(), G_FILE_TEST_IS_REGULAR)) {
        error = "certificate file not found";
        return false;
    }
    GError* gerror = nullptr;
    GTlsCertificate* cert = g_tls_certificate_new_from_file(sourcePath.c_str(), &gerror);
    if (!cert) {
        error = gerror && gerror->message ? gerror->message : "not a certificate";
        g_clear_error(&gerror);
        return false;
    }
    g_object_unref(cert);

    if (!ensureDir(dir, error))
        return false;

    gchar* base = g_path_get_basename(sourcePath.c_str());
    const std::string leaf = uniqueName(dir, base ? base : "certificate.pem");
    g_free(base);
    gchar* dest = g_build_filename(dir.c_str(), leaf.c_str(), nullptr);
    gchar* contents = nullptr;
    gsize length = 0;
    if (!g_file_get_contents(sourcePath.c_str(), &contents, &length, &gerror)) {
        error = gerror && gerror->message ? gerror->message : "could not read certificate";
        g_clear_error(&gerror);
        g_free(dest);
        return false;
    }
    if (!g_file_set_contents(dest, contents, static_cast<gssize>(length), &gerror)) {
        error = gerror && gerror->message ? gerror->message : "could not write certificate";
        g_clear_error(&gerror);
        g_free(contents);
        g_free(dest);
        return false;
    }
    g_free(contents);
    g_free(dest);

    const std::vector<NmNet::Certificate> after = list(dir);
    for (const NmNet::Certificate& entry : after) {
        if (entry.path.size() >= leaf.size()
            && entry.path.compare(entry.path.size() - leaf.size(), leaf.size(), leaf) == 0) {
            out = entry;
            return true;
        }
    }
    error = "certificate was written but could not be listed";
    return false;
}

bool remove(const std::string& dir, int certificateId, std::string& error)
{
    if (certificateId < 1) {
        error = "certificateId is required";
        return false;
    }
    const std::vector<NmNet::Certificate> found = list(dir);
    for (const NmNet::Certificate& entry : found) {
        if (entry.certificateId != certificateId)
            continue;
        if (g_unlink(entry.path.c_str()) != 0) {
            error = "could not delete certificate";
            return false;
        }
        return true;
    }
    error = "certificate not found";
    return false;
}

} // namespace Certificates
