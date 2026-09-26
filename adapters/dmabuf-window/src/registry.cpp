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

#include "dmabuf_window.h"

#include <unistd.h>

#include <mutex>
#include <unordered_map>

namespace dmabuf_window {
namespace {

std::mutex& mutex()
{
    static std::mutex m;
    return m;
}

std::unordered_map<int, Export>& table()
{
    static std::unordered_map<int, Export> t;
    return t;
}

} // namespace

void registryPut(int key, const Export& desc)
{
    if (key < 0 || desc.fd < 0)
        return;

    Export copy = desc;
    copy.fd = ::dup(desc.fd);
    if (copy.fd < 0)
        return;

    std::lock_guard<std::mutex> lock(mutex());
    auto& slot = table()[key];
    if (slot.fd >= 0)
        ::close(slot.fd);
    slot = copy;
}

bool registryTake(int key, Export* out)
{
    // Dup for the caller; keep the registry entry for later acquires.
    if (!out || key < 0)
        return false;

    std::lock_guard<std::mutex> lock(mutex());
    auto it = table().find(key);
    if (it == table().end() || it->second.fd < 0)
        return false;

    *out = it->second;
    out->fd = ::dup(it->second.fd);
    return out->fd >= 0;
}

void registryClear(int key)
{
    std::lock_guard<std::mutex> lock(mutex());
    auto it = table().find(key);
    if (it == table().end())
        return;
    if (it->second.fd >= 0)
        ::close(it->second.fd);
    table().erase(it);
}

} // namespace dmabuf_window
