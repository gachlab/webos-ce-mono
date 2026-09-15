// What com.palm.power reports, read from a fake /sys/class/power_supply.
//
// The status bar and DisplayManager both drop a payload that lacks a field they
// read, so a wrong name here does not show as a wrong number -- it shows as a
// battery icon that never updates. That is why the payload fields are checked
// as strings, not just the numbers behind them.
//
// Measured on the ZBook the service is written for:
//     AC    type=Mains    online=1
//     BAT0  type=Battery  capacity=100  status=Full
#include "power_state.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("  %-66s %s\n", what, ok ? "OK" : "<-- FAIL");
    if (!ok)
        ++g_failures;
}

static bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

// A throwaway tree shaped like /sys/class/power_supply.
struct FakeSysfs {
    std::string root;

    FakeSysfs()
    {
        char tmpl[] = "/tmp/power-state-XXXXXX";
        root = mkdtemp(tmpl);
    }
    ~FakeSysfs()
    {
        std::string cmd = "rm -rf '" + root + "'";
        if (std::system(cmd.c_str()) != 0) { /* best effort */ }
    }
    void file(const std::string& supply, const std::string& name, const std::string& value)
    {
        const std::string dir = root + "/" + supply;
        mkdir(dir.c_str(), 0755);
        std::ofstream(dir + "/" + name) << value << "\n";
    }
};

int main()
{
    std::printf("the laptop this was measured on\n");
    {
        FakeSysfs fs;
        fs.file("AC", "type", "Mains");    fs.file("AC", "online", "1");
        fs.file("BAT0", "type", "Battery"); fs.file("BAT0", "capacity", "100"); fs.file("BAT0", "status", "Full");
        const SysfsPower::PowerState s = SysfsPower::readPowerState(fs.root);
        check(s.hasBattery && s.percent == 100 && s.externalPower, "full battery on AC reads 100% with external power");
        check(SysfsPower::isCharging(s), "a full battery on AC still counts as charging, as on a phone");
    }

    std::printf("\ndischarging and charging\n");
    {
        FakeSysfs fs;
        fs.file("AC", "type", "Mains");    fs.file("AC", "online", "0");
        fs.file("BAT0", "type", "Battery"); fs.file("BAT0", "capacity", "42"); fs.file("BAT0", "status", "Discharging");
        const SysfsPower::PowerState s = SysfsPower::readPowerState(fs.root);
        check(s.percent == 42 && !s.externalPower, "unplugged at 42% reads 42% on battery");
        check(!SysfsPower::isCharging(s), "unplugged is not charging");
        check(contains(SysfsPower::chargerPayload(s, false), "\"USBName\":\"none\""), "unplugged names no charger");
    }
    {
        FakeSysfs fs;
        fs.file("ADP1", "type", "Mains");  fs.file("ADP1", "online", "1");
        fs.file("BAT0", "type", "Battery"); fs.file("BAT0", "capacity", "80"); fs.file("BAT0", "status", "Charging");
        const SysfsPower::PowerState s = SysfsPower::readPowerState(fs.root);
        check(SysfsPower::isCharging(s), "plugged in and charging is charging");
        check(contains(SysfsPower::chargerPayload(s, false), "\"USBName\":\"wall\""), "an AC adapter is reported as a wall charger");
    }
    {
        // A charge threshold holds the battery back while plugged in.
        FakeSysfs fs;
        fs.file("AC", "type", "Mains");    fs.file("AC", "online", "1");
        fs.file("BAT0", "type", "Battery"); fs.file("BAT0", "capacity", "80"); fs.file("BAT0", "status", "Not charging");
        check(SysfsPower::isCharging(SysfsPower::readPowerState(fs.root)), "held back by a charge threshold is still on external power");
    }

    std::printf("\nbatteries that are not this machine's\n");
    {
        FakeSysfs fs;
        fs.file("AC", "type", "Mains");                 fs.file("AC", "online", "1");
        fs.file("BAT0", "type", "Battery");             fs.file("BAT0", "capacity", "63"); fs.file("BAT0", "status", "Charging");
        fs.file("hidpp_battery_0", "type", "Battery");  fs.file("hidpp_battery_0", "scope", "Device");
        fs.file("hidpp_battery_0", "capacity", "5");    fs.file("hidpp_battery_0", "status", "Discharging");
        const SysfsPower::PowerState s = SysfsPower::readPowerState(fs.root);
        check(s.percent == 63 && s.status == "Charging", "a wireless mouse's battery (scope=Device) is ignored");
    }
    {
        FakeSysfs fs;
        fs.file("BAT1", "type", "Battery"); fs.file("BAT1", "present", "0"); fs.file("BAT1", "capacity", "0");
        check(!SysfsPower::readPowerState(fs.root).hasBattery, "an empty battery bay (present=0) is not a battery");
    }

    std::printf("\ntwo batteries\n");
    {
        FakeSysfs fs;
        fs.file("BAT0", "type", "Battery"); fs.file("BAT0", "capacity", "100"); fs.file("BAT0", "status", "Discharging");
        fs.file("BAT0", "energy_now", "20000000"); fs.file("BAT0", "energy_full", "20000000");
        fs.file("BAT1", "type", "Battery"); fs.file("BAT1", "capacity", "10"); fs.file("BAT1", "status", "Discharging");
        fs.file("BAT1", "energy_now", "6000000"); fs.file("BAT1", "energy_full", "60000000");
        check(SysfsPower::readPowerState(fs.root).percent == 33, "combined by energy (26 of 80 Wh = 33%), not by averaging (55%)");
    }

    std::printf("\na desktop with no battery\n");
    {
        FakeSysfs fs;
        fs.file("AC", "type", "Mains"); fs.file("AC", "online", "1");
        const SysfsPower::PowerState s = SysfsPower::readPowerState(fs.root);
        check(!s.hasBattery && s.percent == 100, "no battery reads 100%, never an empty battery");
        check(!SysfsPower::isCharging(s), "no battery is never charging");
        const std::string charger = SysfsPower::chargerPayload(s, false);
        check(contains(charger, "\"USBConnected\":true"), "no battery reports being on external power");
        // PowerdService.js opens its "not charging" alert for USBConnected true
        // with Charging false unless USBName is "pc". Found in review: "wall" here
        // popped that alert two seconds after every start on a desktop.
        check(contains(charger, "\"USBName\":\"pc\""), "no battery names the charger pc, which systemui's not-charging alert skips");
        check(!(contains(charger, "\"Charging\":false") && contains(charger, "\"USBName\":\"wall\"")),
              "no battery never reads as a wall charger that is not charging");
    }
    {
        check(!SysfsPower::readPowerState("/nonexistent/power_supply").hasBattery, "a missing power_supply directory is a machine with no battery");
    }

    std::printf("\nthe payloads HP's callers validate\n");
    {
        SysfsPower::PowerState s;
        s.hasBattery = true; s.percent = 57; s.status = "Discharging"; s.externalPower = false;

        const std::string battery = SysfsPower::batteryPayload(s, false);
        check(contains(battery, "\"percent\":57"), "batteryStatus carries percent (DisplayManager requires it)");
        check(contains(battery, "\"percent_ui\":57"), "batteryStatus carries percent_ui (the status bar reads it)");

        const std::string charger = SysfsPower::chargerPayload(s, false);
        bool allSix = true;
        for (const char* f : {"\"Charging\":", "\"DockConnected\":", "\"DockPower\":",
                              "\"DockSerialNo\":", "\"USBConnected\":", "\"USBName\":"})
            allSix = allSix && contains(charger, f);
        check(allSix, "chargerStatus carries all six fields DisplayManager requires");

        check(!contains(battery, "returnValue") && !contains(charger, "returnValue"), "signal payloads carry no returnValue");
        check(contains(SysfsPower::batteryPayload(s, true), "\"returnValue\":true"), "a query reply does carry returnValue");
    }

    std::printf("\nwhich signals carry the charger state\n");
    {
        // MEASURED with chargerStatus alone: the status bar got every battery
        // update and no charger update, because it listens on USBDockStatus.
        bool charger = false, usbDock = false;
        for (const char* const* n = SysfsPower::chargerSignalNames(); *n; ++n) {
            charger = charger || std::string(*n) == "chargerStatus";
            usbDock = usbDock || std::string(*n) == "USBDockStatus";
        }
        check(charger, "chargerStatus is emitted (DisplayManager listens on it)");
        check(usbDock, "USBDockStatus is emitted (the status bar and systemui listen on it)");
    }

    std::printf("\ntelling a request for state from a state being announced\n");
    {
        SysfsPower::PowerState s;
        s.hasBattery = true; s.percent = 80; s.status = "Charging"; s.externalPower = true;

        check(SysfsPower::isStateRequest("{}"), "DisplayManager's empty {} is a request");
        check(SysfsPower::isStateRequest(nullptr), "no payload at all is a request");
        // The loop this prevents: the service listens on USBDockStatus for
        // requests and also broadcasts on it. Its own broadcast must not count.
        check(!SysfsPower::isStateRequest(SysfsPower::chargerPayload(s, false).c_str()),
              "the charger state it broadcasts is not a request (no re-broadcast loop)");
        check(!SysfsPower::isStateRequest(SysfsPower::batteryPayload(s, false).c_str()),
              "the battery state it broadcasts is not a request");
    }

    std::printf("\n%s\n", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
