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
// com.palm.storage, for a machine with no USB gadget and no partitions of its
// own.
//
// Ours, not HP's: HP's storaged was never in the CE drop (Open webOS released
// its own, 1900 lines of C against udev, nyx and a phone's USB gadget). What is
// HP's is the API, taken from the callers in this tree and from the released
// binary: /diskmode, /erase and the /storaged signals.
//
//   /diskmode  mass storage mode. A laptop does not export its disk over USB,
//              so the queries answer "no host connected, not in mass storage
//              mode" and enterMSM refuses with HP's own message. The shell and
//              the system UI ask before they show anything, so their USB alerts
//              and the brick-mode screen simply never appear -- which is what
//              they did before, except that then every call also logged
//              "com.palm.storage is not running".
//   /erase     the "erase device" flows: Device Info's and Accounts' erase
//              options, the shell's key combos, and the EAS passcode policy.
//              As on the device, the erase is recorded and happens before the
//              session is next started (storaged --apply-erase, from
//              tools/run-lunasysmgr.sh): a service cannot erase the database
//              its own callers are using. HP rebooted for the same reason.
//   /storaged  the signals. PartitionAvail is the one that means anything here:
//              luna-sysservice creates the media folders when it arrives.
//
// What is erased is in erase.cpp, which is where the line between this port's
// data and the user's machine lives.
//

#include "erase.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <glib.h>
#include <glib-unix.h>

#include <lunaservice.h>

namespace {

namespace fs = std::filesystem;
using storaged::Erase;
using storaged::Layout;

const char* kServiceName = "com.palm.storage";
const char* kSignalCategory = "/storaged";
const char* kMediaMountPoint = "/media/internal";

LSPalmService* g_service = nullptr;
GMainLoop* g_loop = nullptr;
Layout g_layout;
std::string g_pendingFile;

void logAndFree(const char* what, LSError& error)
{
    g_warning("storaged: %s: %s", what, error.message ? error.message : "(no message)");
    LSErrorFree(&error);
}

bool reply(LSHandle* handle, LSMessage* message, const std::string& payload)
{
    LSError error;
    LSErrorInit(&error);
    if (!LSMessageReply(handle, message, payload.c_str(), &error)) {
        logAndFree("LSMessageReply", error);
        return false;
    }
    return true;
}

std::string readFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file) {
        return std::string();
    }
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

// ---- /diskmode ---------------------------------------------------------------
//
// Nothing to export: answered rather than left to time out, and in HP's shapes,
// because its callers read `result` before anything else.

bool hostIsConnected(LSHandle* handle, LSMessage* message, void*)
{
    return reply(handle, message, R"({"result":true,"hostIsConnected":false})");
}

bool queryMSMStatus(LSHandle* handle, LSMessage* message, void*)
{
    return reply(handle, message, R"({"result":true,"inMSM":false})");
}

bool enterMSM(LSHandle* handle, LSMessage* message, void*)
{
    // HP's own text for this refusal, which is the same reason: there is no USB
    // connection to export the storage over.
    return reply(handle, message,
                 R"({"result":false,"errorText":"not entering brick mode because no usb connection"})");
}

// The three udev told HP's service about. Nothing here reports cable changes,
// but the methods exist so a caller gets an answer rather than an error.
bool acknowledged(LSHandle* handle, LSMessage* message, void*)
{
    return reply(handle, message, R"({"returnValue":true})");
}

// ---- /erase ------------------------------------------------------------------

bool recordErase(LSHandle* handle, LSMessage* message, void*)
{
    const char* method = LSMessageGetMethod(message);
    Erase erase = Erase::Both;
    if (!method || !storaged::eraseOf(method, &erase)) {
        return reply(handle, message, R"({"returnValue":false,"errorText":"unknown erase"})");
    }

    std::error_code code;
    fs::create_directories(fs::path(g_pendingFile).parent_path(), code);
    std::ofstream pending(g_pendingFile, std::ios::trunc);
    pending << storaged::methodName(erase) << "\n";
    pending.close();
    if (!pending) {
        g_warning("storaged: %s could not be recorded in %s", method, g_pendingFile.c_str());
        return reply(handle, message, R"({"returnValue":false,"errorText":"the erase could not be recorded"})");
    }

    // Every caller reads returnValue and then takes the system down -- sysmgr
    // exits, the apps expect a reboot -- which is what lets the next start
    // erase data nothing is using.
    g_message("storaged: %s recorded; it happens before the next start", method);
    return reply(handle, message, R"({"returnValue":true})");
}

// ---- the erase itself, at the next start -------------------------------------

std::vector<std::string> varEntries(const std::string& varDir)
{
    std::vector<std::string> entries;
    std::error_code code;
    for (const fs::directory_entry& entry : fs::directory_iterator(varDir, code)) {
        entries.push_back(entry.path().string());
    }
    return entries;
}

int applyPendingErase()
{
    const std::string recorded = readFile(g_pendingFile);
    if (recorded.empty()) {
        return 0;
    }
    std::string method = recorded;
    while (!method.empty() && (method.back() == '\n' || method.back() == '\r' || method.back() == ' ')) {
        method.pop_back();
    }
    Erase erase = Erase::Both;
    if (!storaged::eraseOf(method, &erase)) {
        g_warning("storaged: %s is not an erase; ignored", method.c_str());
        std::error_code code;
        fs::remove(g_pendingFile, code);
        return 0;
    }

    // Taken away first: an erase that dies half way through must not run again
    // on the next start, when the state it was asked about is already gone.
    std::error_code code;
    fs::remove(g_pendingFile, code);

    const std::vector<std::string> paths =
        storaged::erasePaths(erase, g_layout, varEntries(g_layout.varDir), readFile(g_layout.historyFile));
    int removed = 0;
    for (const std::string& path : paths) {
        std::error_code failure;
        const std::uintmax_t count = fs::remove_all(path, failure);
        if (failure) {
            g_warning("storaged: %s: %s", path.c_str(), failure.message().c_str());
        } else if (count > 0) {
            ++removed;
        }
    }
    g_message("storaged: %s erased %d of %zu places", method.c_str(), removed, paths.size());
    return 0;
}

// ---- the service --------------------------------------------------------------

LSMethod kDiskModeMethods[] = {
    { "hostIsConnected", hostIsConnected },
    { "queryMSMStatus", queryMSMStatus },
    { "enterMSM", enterMSM },
    { "changed", acknowledged },
    { "avail", acknowledged },
    { "busSuspended", acknowledged },
    { nullptr, nullptr },
};

LSMethod kEraseMethods[] = {
    { "EraseVar", recordErase },
    { "EraseMedia", recordErase },
    { "EraseAll", recordErase },
    { "Wipe", recordErase },
    { nullptr, nullptr },
};

// Declared so a subscriber's addmatch has something to match, as HP's did.
// Only PartitionAvail is ever sent: the others say the storage is being
// exported over USB, which cannot happen here.
LSSignal kSignals[] = {
    { "MSMAvail" },
    { "MSMProgress" },
    { "MSMEntry" },
    { "MSMFscking" },
    { "MSMStatus" },
    { "PartitionAvail" },
    { nullptr },
};

void sendPartitionAvailable()
{
    const std::string uri = std::string("luna://") + kServiceName + kSignalCategory + "/PartitionAvail";
    const std::string payload =
        std::string(R"({"mount_point":")") + kMediaMountPoint + R"(","available":true})";
    LSError error;
    LSErrorInit(&error);
    if (!LSSignalSend(LSPalmServiceGetPublicConnection(g_service), uri.c_str(), payload.c_str(), &error))
        logAndFree("PartitionAvail", error);
}

gboolean quit(gpointer)
{
    g_main_loop_quit(g_loop);
    return G_SOURCE_REMOVE;
}

std::string fromEnv(const char* name, const std::string& fallback)
{
    const char* value = g_getenv(name);
    return value && *value ? value : fallback;
}

void readLayout()
{
    const std::string home = fromEnv("HOME", "/root");
    g_layout.varDir = fromEnv("WEBOS_STORAGED_VAR", "/var");
    g_layout.downloadsDir = fromEnv("XDG_DOWNLOAD_DIR", home + "/Downloads");
    g_layout.historyFile = fromEnv("WEBOS_DOWNLOADMANAGER_DATA", "/var/palm/data/com.palm.downloadmanager")
                         + "/history.json";
    g_pendingFile = g_layout.varDir + "/luna/storaged-erase";
}

} // namespace

int main(int argc, char** argv)
{
    readLayout();

    if (argc > 1 && std::string(argv[1]) == "--apply-erase") {
        return applyPendingErase();
    }

    g_loop = g_main_loop_new(nullptr, FALSE);

    LSError error;
    LSErrorInit(&error);
    if (!LSRegisterPalmService(kServiceName, &g_service, &error)) {
        logAndFree("LSRegisterPalmService", error);
        return 1;
    }
    if (!LSPalmServiceRegisterCategory(g_service, "/diskmode", kDiskModeMethods, kDiskModeMethods,
                                       nullptr, nullptr, &error)) {
        logAndFree("/diskmode", error);
        return 1;
    }
    LSErrorInit(&error);
    if (!LSPalmServiceRegisterCategory(g_service, "/erase", kEraseMethods, kEraseMethods,
                                       nullptr, nullptr, &error)) {
        logAndFree("/erase", error);
        return 1;
    }
    LSErrorInit(&error);
    if (!LSPalmServiceRegisterCategory(g_service, kSignalCategory, nullptr, nullptr,
                                       kSignals, nullptr, &error)) {
        logAndFree(kSignalCategory, error);
        return 1;
    }
    LSErrorInit(&error);
    if (!LSGmainAttachPalmService(g_service, g_loop, &error)) {
        logAndFree("LSGmainAttachPalmService", error);
        return 1;
    }

    // The media is there for good here, so this is sent once, at startup:
    // luna-sysservice makes the media folders when it arrives, and the
    // application installer waits for it before it will install anything.
    sendPartitionAvailable();

    g_unix_signal_add(SIGTERM, quit, nullptr);
    g_unix_signal_add(SIGINT, quit, nullptr);

    g_message("storaged: com.palm.storage up");
    g_main_loop_run(g_loop);

    LSErrorInit(&error);
    if (!LSUnregisterPalmService(g_service, &error))
        logAndFree("LSUnregisterPalmService", error);
    g_main_loop_unref(g_loop);
    return 0;
}
