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
// com.palm.power, answered from /sys/class/power_supply.
//
// On a device this was powerd. Nothing in the CE drop provides it, so on the
// desktop the status bar logged "Service does not exist: com.palm.power." and
// never learned the battery level. Once a com.palm.power exists, though, every
// other caller that has been failing quietly starts talking to it, so this
// answers the whole interface they use -- not only the battery:
//
//   /com/palm/power  batteryStatusQuery, chargerStatusQuery  -- the status bar
//                    signals batteryStatus                    -- status bar,
//                                                    DisplayManager, systemui
//                            chargerStatus, USBDockStatus     -- the same
//                                                    charger state under both
//                                                    names; see power_state.h
//                    identify, suspendRequestRegister,
//                    prepareSuspendRegister, *Ack             -- SuspendBlocker
//                    activityStart, activityEnd               -- WebAppManager,
//                                                    SoundPlayer, InputManager
//   /timeout         set, clear                               -- luna-sysservice
//   /shutdown        machineOff, machineReboot                -- the power menu
//
// Two things it deliberately never does:
//
//   * emit suspendRequest or prepareSuspend. There is no suspend here, and
//     SuspendBlocker.cpp carries HP's own FIXME about hangs on that path.
//   * power off or reboot the machine. machineOff ends the webOS session and
//     machineReboot restarts it; tools/webos-session.sh does the restarting.
//
// The status bar and DisplayManager do not poll: they addmatch the two signals.
// A JavaScript service cannot emit a luna-service2 signal -- palmbus exposes no
// LSSignalSend -- which is why this is C++.
//

#include "power_state.h"

#include <luna-service2/lunaservice.h>

#include <glib.h>
#include <glib-unix.h>

#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace {

const char kServiceName[] = "com.palm.power";
const char kPowerCategory[] = "/com/palm/power";
const char kSupplyRoot[] = "/sys/class/power_supply";

// The kernel does not push power_supply changes anywhere a namespaced process can
// cheaply listen, and a battery moves one percent in minutes. Five seconds keeps
// a cable being pulled feeling immediate without keeping the CPU awake.
const guint kPollSeconds = 5;

// Where tools/webos-session.sh looks, after the shell exits, for whether it was
// asked to restart. It exports the path; the fallback matches its default log
// directory for a session started by hand.
const char kDefaultSessionRequest[] = "/tmp/webos/session-request";

GMainLoop* g_loop = nullptr;
LSPalmService* g_service = nullptr;
SysfsPower::PowerState g_state;
bool g_haveState = false;
unsigned g_nextClientId = 1;

LSHandle* privateBus()
{
    return LSPalmServiceGetPrivateConnection(g_service);
}

void logAndFree(const char* where, LSError& error)
{
    g_warning("sysfs-powerd: %s: %s", where, error.message ? error.message : "(no message)");
    LSErrorFree(&error);
}

bool reply(LSHandle* sh, LSMessage* message, const std::string& payload)
{
    LSError error;
    LSErrorInit(&error);
    if (!LSMessageReply(sh, message, payload.c_str(), &error))
        logAndFree("LSMessageReply", error);
    return true;
}

bool replyOk(LSHandle* sh, LSMessage* message)
{
    return reply(sh, message, "{\"returnValue\":true}");
}

void sendSignal(const char* method, const std::string& payload)
{
    const std::string uri = std::string("palm://") + kServiceName + kPowerCategory + "/" + method;
    LSError error;
    LSErrorInit(&error);
    if (!LSSignalSend(privateBus(), uri.c_str(), payload.c_str(), &error))
        logAndFree(method, error);
}

void broadcast()
{
    sendSignal("batteryStatus", SysfsPower::batteryPayload(g_state, false));
    const std::string charger = SysfsPower::chargerPayload(g_state, false);
    for (const char* const* name = SysfsPower::chargerSignalNames(); *name; ++name)
        sendSignal(*name, charger);
}

// DisplayManager asks three times in a row when com.palm.power appears
// (chargerStatusQuery, USBDockStatus and batteryStatusQuery), and answering each
// one sent the whole state three times: MEASURED, three identical battery and
// charger signals within the same millisecond. Requests that arrive together get
// one broadcast.
guint g_broadcastPending = 0;

gboolean broadcastNow(gpointer)
{
    g_broadcastPending = 0;
    broadcast();
    return G_SOURCE_REMOVE;
}

void scheduleBroadcast()
{
    if (!g_broadcastPending)
        g_broadcastPending = g_idle_add(broadcastNow, nullptr);
}

gboolean poll(gpointer)
{
    const SysfsPower::PowerState now = SysfsPower::readPowerState(kSupplyRoot);
    if (!g_haveState || now != g_state) {
        g_state = now;
        g_haveState = true;
        g_message("sysfs-powerd: battery=%s percent=%d status=%s external=%s",
                  g_state.hasBattery ? "yes" : "no", g_state.percent,
                  g_state.status.c_str(), g_state.externalPower ? "yes" : "no");
        broadcast();
    }
    return G_SOURCE_CONTINUE;
}

// --- /com/palm/power ------------------------------------------------------

bool batteryStatusQuery(LSHandle* sh, LSMessage* message, void*)
{
    return reply(sh, message, SysfsPower::batteryPayload(g_state, true));
}

bool chargerStatusQuery(LSHandle* sh, LSMessage* message, void*)
{
    return reply(sh, message, SysfsPower::chargerPayload(g_state, true));
}

// SuspendBlocker drops the whole registration unless it gets both of these back
// ("Failed to subscribe to powerd").
bool identify(LSHandle* sh, LSMessage* message, void*)
{
    char payload[96];
    std::snprintf(payload, sizeof payload,
                  "{\"returnValue\":true,\"subscribed\":true,\"clientId\":\"%u\"}",
                  g_nextClientId++);
    return reply(sh, message, payload);
}

// Accepted and forgotten: registering for suspend notifications, acknowledging
// them, and holding the device awake for an activity all mean nothing on a
// machine this service never suspends.
bool acceptAndIgnore(LSHandle* sh, LSMessage* message, void*)
{
    return replyOk(sh, message);
}

LSMethod kPowerMethods[] = {
    { "batteryStatusQuery", batteryStatusQuery },
    { "chargerStatusQuery", chargerStatusQuery },
    { "identify", identify },
    { "suspendRequestRegister", acceptAndIgnore },
    { "prepareSuspendRegister", acceptAndIgnore },
    { "suspendRequestAck", acceptAndIgnore },
    { "prepareSuspendAck", acceptAndIgnore },
    { "activityStart", acceptAndIgnore },
    { "activityEnd", acceptAndIgnore },
    { },
};

LSSignal kPowerSignals[] = {
    { "batteryStatus" },
    { "chargerStatus" },
    { "USBDockStatus" },
    { },
};

// --- /timeout ---------------------------------------------------------------

// luna-sysservice schedules its periodic NTP check here. Answering success
// without scheduling anything is what already happens today, minus the error in
// the log: the host keeps its own clock.
LSMethod kTimeoutMethods[] = {
    { "set", acceptAndIgnore },
    { "clear", acceptAndIgnore },
    { },
};

// --- /shutdown ----------------------------------------------------------------

// Record what was asked for where the session supervisor reads it, then end the
// shell. The host is never touched.
gboolean endSession(gpointer data)
{
    const char* request = static_cast<const char*>(data);

    const char* path = std::getenv("WEBOS_SESSION_REQUEST");
    if (!path || !*path)
        path = kDefaultSessionRequest;

    std::string dir(path);
    const std::string::size_type slash = dir.rfind('/');
    if (slash != std::string::npos && slash > 0) {
        dir.resize(slash);
        g_mkdir_with_parents(dir.c_str(), 0755);
    }
    std::ofstream(path) << request << "\n";

    // SystemService::shutdownDevice already quits the shell after calling
    // machineOff; the alerts in luna-systemui do not. Ending it here covers both,
    // and a second TERM to a shell that is already on its way out is harmless.
    GDir* proc = g_dir_open("/proc", 0, nullptr);
    int ended = 0;
    if (proc) {
        while (const char* name = g_dir_read_name(proc)) {
            if (!g_ascii_isdigit(name[0]))
                continue;
            std::string comm;
            if (SysfsPower::readLine(std::string("/proc/") + name + "/comm", comm)
                && comm == "LunaSysMgr") {
                if (kill(static_cast<pid_t>(std::atol(name)), SIGTERM) == 0)
                    ++ended;
            }
        }
        g_dir_close(proc);
    }
    g_message("sysfs-powerd: session %s requested, wrote %s, ended %d shell process(es)",
              request, path, ended);
    return G_SOURCE_REMOVE;
}

// Mutable arrays rather than literals: g_idle_add takes a plain gpointer, and a
// string literal cannot be handed over as one without casting its const away.
char kPowerOffRequest[] = "poweroff";
char kRestartRequest[] = "restart";

bool machineOff(LSHandle* sh, LSMessage* message, void*)
{
    replyOk(sh, message);
    g_idle_add(endSession, kPowerOffRequest);
    return true;
}

bool machineReboot(LSHandle* sh, LSMessage* message, void*)
{
    replyOk(sh, message);
    g_idle_add(endSession, kRestartRequest);
    return true;
}

LSMethod kShutdownMethods[] = {
    { "machineOff", machineOff },
    { "machineReboot", machineReboot },
    { },
};

// --- requests to re-broadcast ---------------------------------------------------

// When com.palm.power appears, DisplayManager sends signals asking for the
// current state (powerdServiceNotification): .../com/palm/power/
// chargerStatusQuery, batteryStatusQuery and USBDockStatus. powerd answered by
// broadcasting; so does this.
bool onStateRequest(LSHandle*, LSMessage* message, void*)
{
    const char* category = LSMessageGetCategory(message);
    if (!category || std::strcmp(category, kPowerCategory) != 0)
        return true; // the addmatch's own {"returnValue":true}

    // USBDockStatus is both a request DisplayManager sends and a state this
    // service announces, so this addmatch also delivers our own broadcasts.
    // Answering those would re-broadcast in a loop. Two independent guards: who
    // sent it, and whether it carries a state at all.
    const char* sender = LSMessageGetSenderServiceName(message);
    if (sender && std::strcmp(sender, kServiceName) == 0)
        return true;
    if (!SysfsPower::isStateRequest(LSMessageGetPayload(message)))
        return true;

    if (g_haveState)
        scheduleBroadcast();
    return true;
}

void listenForStateRequests()
{
    for (const char* method : { "batteryStatusQuery", "chargerStatusQuery", "USBDockStatus" }) {
        char payload[128];
        std::snprintf(payload, sizeof payload,
                      "{\"category\":\"%s\",\"method\":\"%s\"}", kPowerCategory, method);
        LSError error;
        LSErrorInit(&error);
        if (!LSCall(privateBus(), "palm://com.palm.bus/signal/addmatch", payload,
                    onStateRequest, nullptr, nullptr, &error))
            logAndFree("addmatch", error);
    }
}

gboolean quit(gpointer)
{
    g_main_loop_quit(g_loop);
    return G_SOURCE_REMOVE;
}

bool registerCategory(const char* category, LSMethod* methods, LSSignal* signals)
{
    LSError error;
    LSErrorInit(&error);
    if (!LSPalmServiceRegisterCategory(g_service, category, nullptr, methods, signals,
                                       nullptr, &error)) {
        logAndFree(category, error);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    g_loop = g_main_loop_new(nullptr, FALSE);

    LSError error;
    LSErrorInit(&error);
    if (!LSRegisterPalmService(kServiceName, &g_service, &error)) {
        logAndFree("LSRegisterPalmService", error);
        return 1;
    }

    if (!registerCategory(kPowerCategory, kPowerMethods, kPowerSignals)
        || !registerCategory("/timeout", kTimeoutMethods, nullptr)
        || !registerCategory("/shutdown", kShutdownMethods, nullptr))
        return 1;

    LSErrorInit(&error);
    if (!LSGmainAttachPalmService(g_service, g_loop, &error)) {
        logAndFree("LSGmainAttachPalmService", error);
        return 1;
    }

    // The state first, so neither a query nor a re-broadcast request can see an
    // empty one.
    poll(nullptr);
    listenForStateRequests();
    g_timeout_add_seconds(kPollSeconds, poll, nullptr);

    g_unix_signal_add(SIGTERM, quit, nullptr);
    g_unix_signal_add(SIGINT, quit, nullptr);

    g_message("sysfs-powerd: com.palm.power up");
    g_main_loop_run(g_loop);

    LSErrorInit(&error);
    if (!LSUnregisterPalmService(g_service, &error))
        logAndFree("LSUnregisterPalmService", error);
    g_main_loop_unref(g_loop);
    return 0;
}
