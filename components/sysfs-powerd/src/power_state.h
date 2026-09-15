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

#ifndef SYSFS_POWERD_POWER_STATE_H
#define SYSFS_POWERD_POWER_STATE_H

//
// What the kernel says about power, and the two payloads HP's code expects
// from com.palm.power. Header-only and free of the bus on purpose, so the
// decisions can be tested against a fake /sys tree.
//
// On a device these payloads came from powerd reading the fuel gauge. Nothing in
// the CE drop provides com.palm.power, so on the desktop the status bar logged
// "Service does not exist: com.palm.power." and knew nothing about the battery.
//
// The field names are not chosen here; each one is read by a caller that drops
// the whole payload when it is missing:
//
//   batteryStatus  StatusBarServicesConnector.cpp reads "percent_ui";
//                  DisplayManager.cpp validates {"percent": integer}.
//   chargerStatus  StatusBarServicesConnector.cpp reads "Charging";
//                  DisplayManager.cpp validates all six of Charging,
//                  DockConnected, DockPower, DockSerialNo, USBConnected and
//                  USBName; PowerdService.js reads USBName "wall" or "pc".
//
// One thing to know before turning schema validation on. DisplayManager's
// schemas are strict ("additionalProperties":false): batteryStatus is declared as
// {"percent": integer} and nothing else. It gets percent_ui as well, because the
// status bar reads only that. That works because luna.conf sets
// schemaValidationOption=0, which is EIgnore and skips validation entirely. Set
// it to 1 or 2 and DisplayManager starts rejecting every battery update, while
// the status bar keeps working and hides the fact.
//

#include <dirent.h>

#include <cstdio>
#include <fstream>
#include <string>

namespace SysfsPower {

struct PowerState {
    bool hasBattery = false;
    int percent = 100;          // 0..100; 100 when there is no battery
    std::string status;         // the kernel's word: Charging, Discharging, Full, Not charging, Unknown
    bool externalPower = false; // any Mains or USB supply reporting online=1

    bool operator==(const PowerState& o) const
    {
        return hasBattery == o.hasBattery && percent == o.percent
            && status == o.status && externalPower == o.externalPower;
    }
    bool operator!=(const PowerState& o) const { return !(*this == o); }
};

inline bool readLine(const std::string& path, std::string& out)
{
    std::ifstream in(path);
    if (!in)
        return false;
    std::getline(in, out);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
        out.pop_back();
    return true;
}

inline bool readLong(const std::string& path, long long& out)
{
    std::string s;
    if (!readLine(path, s) || s.empty())
        return false;
    char* end = nullptr;
    out = std::strtoll(s.c_str(), &end, 10);
    return end && *end == '\0';
}

inline int clampPercent(long long v)
{
    return v < 0 ? 0 : (v > 100 ? 100 : static_cast<int>(v));
}

// root is normally "/sys/class/power_supply"; tests pass a fake tree.
inline PowerState readPowerState(const std::string& root)
{
    PowerState state;

    // Several system batteries are combined by energy when every one reports it,
    // which is what the percentage means; otherwise by averaging capacity.
    long long energyNow = 0, energyFull = 0, capacitySum = 0;
    int batteries = 0;
    bool allHaveEnergy = true;
    bool anyCharging = false, anyDischarging = false, allFull = true;

    DIR* dir = opendir(root.c_str());
    if (!dir)
        return state;

    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..")
            continue;
        const std::string base = root + "/" + name + "/";

        std::string type;
        if (!readLine(base + "type", type))
            continue;

        if (type == "Mains" || type == "USB" || type == "USB_C" || type == "USB_PD") {
            long long online = 0;
            if (readLong(base + "online", online) && online == 1)
                state.externalPower = true;
            continue;
        }

        if (type != "Battery")
            continue;

        // A wireless mouse or a headset reports a Battery too, with
        // scope=Device. Those are not what powers this machine.
        std::string scope;
        if (readLine(base + "scope", scope) && scope == "Device")
            continue;

        long long present = 1;
        if (readLong(base + "present", present) && present == 0)
            continue;

        long long capacity = -1;
        if (!readLong(base + "capacity", capacity))
            continue;

        ++batteries;
        capacitySum += clampPercent(capacity);

        long long now = 0, full = 0;
        if ((readLong(base + "energy_now", now) && readLong(base + "energy_full", full))
            || (readLong(base + "charge_now", now) && readLong(base + "charge_full", full))) {
            energyNow += now;
            energyFull += full;
        } else {
            allHaveEnergy = false;
        }

        std::string status;
        readLine(base + "status", status);
        if (status == "Charging")
            anyCharging = true;
        else if (status == "Discharging")
            anyDischarging = true;
        if (status != "Full")
            allFull = false;
    }
    closedir(dir);

    if (batteries == 0)
        return state;

    state.hasBattery = true;
    if (allHaveEnergy && energyFull > 0)
        state.percent = clampPercent((energyNow * 100 + energyFull / 2) / energyFull);
    else
        state.percent = clampPercent((capacitySum + batteries / 2) / batteries);

    state.status = anyCharging ? "Charging"
                 : anyDischarging ? "Discharging"
                 : allFull ? "Full"
                 : "Not charging";
    return state;
}

// Whether webOS should consider the device charging. A plugged-in battery that
// is full, or held back by a charge threshold ("Not charging"), is still on
// external power: on a phone that showed as charging, and PowerdService.js keys
// its "Charging Battery" banner and its "not charging" alert off this flag.
inline bool isCharging(const PowerState& s)
{
    if (!s.hasBattery)
        return false;
    return s.externalPower && s.status != "Discharging";
}

// The payload of the batteryStatus signal and of a batteryStatusQuery reply.
// A machine with no battery reports 100: the status bar has no way to hide its
// battery icon, and an empty one would be wrong, while " ? " is what it shows
// for a powerd that is not there at all.
inline std::string batteryPayload(const PowerState& s, bool withReturnValue)
{
    char buf[128];
    std::snprintf(buf, sizeof buf, "{%s\"percent\":%d,\"percent_ui\":%d}",
                  withReturnValue ? "\"returnValue\":true," : "",
                  s.percent, s.percent);
    return buf;
}

// The payload of the chargerStatus signal and of a chargerStatusQuery reply.
// There is no dock on a desktop. External power is reported as a wall charger,
// which is what a laptop's AC adapter is: PowerdService.js distinguishes "wall"
// from "pc" only to skip its "not charging" alert for the latter.
inline std::string chargerPayload(const PowerState& s, bool withReturnValue)
{
    const bool plugged = s.externalPower || !s.hasBattery;
    char buf[256];
    std::snprintf(buf, sizeof buf,
                  "{%s\"Charging\":%s,\"DockConnected\":false,\"DockPower\":false,"
                  "\"DockSerialNo\":\"\",\"USBConnected\":%s,\"USBName\":\"%s\"}",
                  withReturnValue ? "\"returnValue\":true," : "",
                  isCharging(s) ? "true" : "false",
                  plugged ? "true" : "false",
                  plugged ? "wall" : "none");
    return buf;
}

// The signals that carry the charger payload. Two names for one state, because
// HP's callers do not agree on which one to listen to:
//
//   chargerStatus  DisplayManager.cpp (JSON_CHARGER_SIGNAL_ADDMATCH)
//   USBDockStatus  StatusBarServicesConnector.cpp -- under a comment that says
//                  "Register for charger status updates" -- and PowerdService.js
//
// MEASURED with only chargerStatus emitted: the status bar received every
// battery update and not one charger update, so unplugging the laptop moved the
// percentage while the charging bolt stayed on.
inline const char* const* chargerSignalNames()
{
    static const char* const names[] = { "chargerStatus", "USBDockStatus", nullptr };
    return names;
}

// Whether a message on one of the state signals is someone asking for the
// current state, rather than a state being announced.
//
// This matters because the names overlap. When com.palm.power appears,
// DisplayManager sends an empty {} on .../com/palm/power/USBDockStatus to ask for
// a re-broadcast, and the service answers by broadcasting on USBDockStatus
// itself. Treating its own broadcast as another request would re-broadcast
// forever. A request carries nothing; a state always carries "Charging" or
// "percent".
inline bool isStateRequest(const char* payload)
{
    if (!payload)
        return true;
    const std::string p(payload);
    return p.find("\"Charging\"") == std::string::npos
        && p.find("\"percent\"") == std::string::npos;
}

} // namespace SysfsPower

#endif // SYSFS_POWERD_POWER_STATE_H
