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

#ifndef ROMAPPPATH_H
#define ROMAPPPATH_H

#include <string>

// Whether an app came with the system, which HP decided by its folder starting
// with "/usr": everything under /usr was the phone's read-only image, while
// downloaded apps live under /media/cryptofs/apps and /var.
//
// This port runs from a rootfs under a prefix -- .../build/rootfs, or the
// AppImage's own directory -- and every path in luna.conf carries it, so no app
// folder started with "/usr" and every app, the system's own included, was
// taken for one the user could remove. The prefix is recovered from where the
// system UI is configured to live, whose path under the rootfs is fixed, and
// the "/usr" test is made below it.

// The rootfs prefix, from the configured system UI path; empty when that path
// is the unprefixed one, or does not end the way it should.
inline std::string rootfsPrefix(std::string systemPath)
{
    static const std::string kSystemUi = "/usr/lib/luna/system/luna-systemui";
    while (systemPath.size() > 1 && systemPath[systemPath.size() - 1] == '/')
        systemPath.erase(systemPath.size() - 1);
    if (systemPath.size() < kSystemUi.size()
        || systemPath.compare(systemPath.size() - kSystemUi.size(), kSystemUi.size(), kSystemUi) != 0)
        return std::string();
    return systemPath.substr(0, systemPath.size() - kSystemUi.size());
}

// Whether folderPath is in the system's image: prefix + "/usr" and below.
inline bool isRomAppPath(const std::string& folderPath, const std::string& prefix)
{
    const std::string rom = prefix + "/usr";
    if (folderPath.compare(0, rom.size(), rom) != 0)
        return false;
    return folderPath.size() == rom.size() || folderPath[rom.size()] == '/';
}

#endif
