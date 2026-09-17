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

#ifndef STORAGED_ERASE_H
#define STORAGED_ERASE_H

#include <string>
#include <vector>

//
// What each of com.palm.storage's erase methods erases, and where the line is.
//
// On a device these wiped partitions: /var (the data partition), /media/internal
// (the USB-visible storage), or both, and `Wipe` overwrote them. This port owns
// no partition. What it owns is its own data directory -- db8, the preferences,
// what its services keep -- and the files it downloaded into the user's
// Downloads folder, which is where /media/internal points (see
// com.palm.downloadmanager's paths.ts). Nothing else on the host is ever
// touched, and `ours` is what decides that; the rest of the machine is not this
// port's to erase.
//
// Free of luna-service2 and of the filesystem, so tests/storaged-erase.cpp can
// check every decision against a temporary tree.
//
namespace storaged {

enum class Erase {
    // HP's /var: everything the port keeps about itself.
    Var,
    // HP's /media/internal: here, only the files this port downloaded.
    Media,
    Both,
};

// The method names HP's service answered. `Wipe` is `EraseAll` with no way to
// overwrite what is freed: on a host filesystem that is the operating system's
// to decide, not ours.
const char* methodName(Erase erase);
bool eraseOf(const std::string& method, Erase* erase);

struct Layout {
    // The port's own data directory (/var inside the session).
    std::string varDir;
    // The user's Downloads folder, which /media/internal maps onto.
    std::string downloadsDir;
    // com.palm.downloadmanager's history, which says which files in there are
    // this port's doing.
    std::string historyFile;
};

// Whether `path` is the port's to erase: inside its data directory, or a file
// in the Downloads folder (never the folder itself).
bool ours(const std::string& path, const Layout& layout);

// The files com.palm.downloadmanager recorded, from its history as JSON.
std::vector<std::string> downloadedFiles(const std::string& historyJson);

// Everything `erase` removes, in order. `entries` lists what is in varDir (its
// contents are erased, not the directory itself) and `historyJson` is the
// download history, empty when there is none.
std::vector<std::string> erasePaths(Erase erase, const Layout& layout,
                                    const std::vector<std::string>& varEntries,
                                    const std::string& historyJson);

} // namespace storaged

#endif
