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

#include "erase.h"

#include <filesystem>

#include <cjson/json.h>

namespace storaged {
namespace {

namespace fs = std::filesystem;

// Lexical, not the filesystem's: this decides about paths, including ones that
// are already gone. ".." is resolved, so a recorded "downloads/../../etc" is
// outside and refused.
fs::path cleaned(const std::string& path)
{
    return fs::path(path).lexically_normal();
}

bool inside(const fs::path& path, const std::string& directory)
{
    if (directory.empty()) {
        return false;
    }
    const fs::path root = cleaned(directory);
    const fs::path relative = path.lexically_relative(root);
    return !relative.empty() && *relative.begin() != ".." && relative != ".";
}

} // namespace

const char* methodName(Erase erase)
{
    switch (erase) {
    case Erase::Var:   return "EraseVar";
    case Erase::Media: return "EraseMedia";
    case Erase::Both:  return "EraseAll";
    }
    return "EraseAll";
}

bool eraseOf(const std::string& method, Erase* erase)
{
    if (method == "EraseVar") {
        *erase = Erase::Var;
    } else if (method == "EraseMedia") {
        *erase = Erase::Media;
    } else if (method == "EraseAll" || method == "Wipe") {
        *erase = Erase::Both;
    } else {
        return false;
    }
    return true;
}

bool ours(const std::string& path, const Layout& layout)
{
    const fs::path candidate = cleaned(path);
    if (inside(candidate, layout.varDir)) {
        return true;
    }
    // A downloaded file, never the folder itself and never a directory tree in
    // it that the user made.
    return inside(candidate, layout.downloadsDir) && !fs::is_directory(candidate);
}

std::vector<std::string> downloadedFiles(const std::string& historyJson)
{
    std::vector<std::string> files;
    if (historyJson.empty()) {
        return files;
    }
    json_object* root = json_tokener_parse(historyJson.c_str());
    if (!root || is_error(root)) {
        return files;
    }
    json_object* entries = json_object_object_get(root, "entries");
    if (entries && !is_error(entries) && json_object_is_type(entries, json_type_array)) {
        const int count = json_object_array_length(entries);
        for (int i = 0; i < count; ++i) {
            json_object* entry = json_object_array_get_idx(entries, i);
            json_object* record = entry ? json_object_object_get(entry, "record") : nullptr;
            json_object* target = record ? json_object_object_get(record, "target") : nullptr;
            if (target && !is_error(target) && json_object_is_type(target, json_type_string)) {
                const char* path = json_object_get_string(target);
                if (path && *path) {
                    files.push_back(path);
                }
            }
        }
    }
    json_object_put(root);
    return files;
}

std::vector<std::string> erasePaths(Erase erase, const Layout& layout,
                                    const std::vector<std::string>& varEntries,
                                    const std::string& historyJson)
{
    std::vector<std::string> paths;
    const auto take = [&paths, &layout](const std::string& path) {
        if (ours(path, layout)) {
            paths.push_back(path);
        }
    };

    if (erase == Erase::Media || erase == Erase::Both) {
        for (const std::string& file : downloadedFiles(historyJson)) {
            take(file);
        }
        // What the history itself says, so a wiped download folder is not
        // offered back as "already downloaded". EraseVar takes it along with
        // the rest of the data directory.
        if (erase == Erase::Media) {
            take(layout.historyFile);
        }
    }
    if (erase == Erase::Var || erase == Erase::Both) {
        for (const std::string& entry : varEntries) {
            take(entry);
        }
    }
    return paths;
}

} // namespace storaged
