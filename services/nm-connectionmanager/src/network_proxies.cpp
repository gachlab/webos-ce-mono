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

#include "network_proxies.h"

#include <cjson/json.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <fstream>

namespace NetworkProxies {

std::string path()
{
    const char* override = g_getenv("WEBOS_NETWORK_PROXIES");
    if (override && override[0])
        return override;
    gchar* file = g_build_filename(g_get_user_data_dir(), "webos-ce", "network-proxies.json",
                                   nullptr);
    std::string out = file;
    g_free(file);
    return out;
}

static std::string stringField(json_object* object, const char* name)
{
    if (!object)
        return {};
    json_object* value = json_object_object_get(object, name);
    if (!value || is_error(value) || !json_object_is_type(value, json_type_string))
        return {};
    return json_object_get_string(value);
}

static NmNet::ProxyInfo proxyOf(json_object* object)
{
    NmNet::ProxyInfo info;
    info.networkTechnology = stringField(object, "networkTechnology");
    info.proxyScope = stringField(object, "proxyScope");
    info.proxyConfigType = stringField(object, "proxyConfigType");
    info.proxyServer = stringField(object, "proxyServer");
    info.proxyAutoConfigUrl = stringField(object, "proxyAutoConfigUrl");
    json_object* port = json_object_object_get(object, "proxyPort");
    if (port && !is_error(port) && json_object_is_type(port, json_type_int)) {
        info.proxyPort = json_object_get_int(port);
        info.hasPort = true;
    }
    json_object* secured = json_object_object_get(object, "isProxySecured");
    if (secured && !is_error(secured) && json_object_is_type(secured, json_type_boolean)) {
        info.isProxySecured = json_object_get_boolean(secured);
        info.hasSecured = true;
    }
    return info;
}

std::vector<NmNet::ProxyInfo> load(const std::string& filePath)
{
    std::vector<NmNet::ProxyInfo> out;
    gchar* contents = nullptr;
    gsize length = 0;
    if (!g_file_get_contents(filePath.c_str(), &contents, &length, nullptr) || !contents)
        return out;
    json_object* root = json_tokener_parse(contents);
    g_free(contents);
    if (!root || is_error(root))
        return out;
    json_object* list = json_object_object_get(root, "proxyInfoList");
    if (list && !is_error(list) && json_object_is_type(list, json_type_array)) {
        const int n = json_object_array_length(list);
        for (int i = 0; i < n; ++i) {
            json_object* entry = json_object_array_get_idx(list, i);
            if (!entry || is_error(entry) || !json_object_is_type(entry, json_type_object))
                continue;
            NmNet::ProxyInfo info = proxyOf(entry);
            if (info.networkTechnology.empty() || info.proxyScope.empty()
                || info.proxyConfigType.empty())
                continue;
            out.push_back(info);
        }
    }
    json_object_put(root);
    return out;
}

bool save(const std::string& filePath, const std::vector<NmNet::ProxyInfo>& list,
          std::string& error)
{
    gchar* dir = g_path_get_dirname(filePath.c_str());
    if (dir) {
        if (g_mkdir_with_parents(dir, 0700) != 0) {
            error = std::string("could not create ") + dir;
            g_free(dir);
            return false;
        }
        g_free(dir);
    }
    const std::string body = NmNet::proxiesConfigPayload(list);
    std::ofstream out(filePath, std::ios::trunc);
    if (!out) {
        error = "could not write " + filePath;
        return false;
    }
    out << body << '\n';
    if (!out) {
        error = "could not write " + filePath;
        return false;
    }
    return true;
}

} // namespace NetworkProxies
