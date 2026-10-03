// What com.palm.btmonitor and com.palm.bluetooth answer, from a fake BlueZ
// state. The mapping (bluez_state.h) is header-only and free of both buses, so
// every decision here is checked without a D-Bus daemon and without ls-hubd --
// the way network-state.cpp checks the connection manager's.
//
// The payloads are checked as strings rather than through a parser, for the
// same reason the network-state test does it: HP's StatusBarServicesConnector
// reads named fields out of these objects with json_object_object_get and acts
// on exactly the spellings it knows ("radio", "trusteddevices", "notifn...",
// the seven profile names). A renamed field is not a parse error there, it is a
// radio that never toggles or a device list that stays empty.
//
// The UUID->profile cases are pinned against real devices on the development
// machine: an AKG Y500 headset reports 0000110b (A2DP sink), 0000110e (AVRCP)
// and 0000111e (Handsfree), so a2dp and hf are the profiles it carries.
#include "bluez_state.h"

#include <cstdio>
#include <string>

using namespace BtState;

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("  %-70s %s\n", what, ok ? "OK" : "<-- FAIL");
    if (!ok)
        ++g_failures;
}

static bool has(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

// A headset like the one measured: trusted, bonded, connected, carrying A2DP,
// AVRCP and Handsfree, with a battery and BlueZ's resolved icon.
static Device headset()
{
    Device d;
    d.objectPath = "/org/bluez/hci0/dev_F8_DF_15_F2_29_ED";
    d.address = "F8:DF:15:F2:29:ED";
    d.name = "AKG Y500 WIRELESS";
    d.cod = 2360324;
    d.connected = true;
    d.paired = true;
    d.trusted = true;
    d.bonded = true;
    d.uuids = { "0000110b-0000-1000-8000-00805f9b34fb",
                "0000110e-0000-1000-8000-00805f9b34fb",
                "0000111e-0000-1000-8000-00805f9b34fb" };
    d.icon = "audio-headset";
    d.battery = 60;
    d.addressType = "public";
    return d;
}

static void testProfileFromUuid()
{
    std::printf("profileFromUuid: HP's names from SIG UUIDs\n");
    // Full-length SIG UUIDs, the shape BlueZ reports.
    check(profileFromUuid("0000111f-0000-1000-8000-00805f9b34fb") == "hfg", "111f is hfg (audio gateway)");
    check(profileFromUuid("0000111e-0000-1000-8000-00805f9b34fb") == "hf", "111e is hf (handsfree)");
    check(profileFromUuid("0000110b-0000-1000-8000-00805f9b34fb") == "a2dp", "110b is a2dp (sink)");
    check(profileFromUuid("0000110a-0000-1000-8000-00805f9b34fb") == "a2dp", "110a is a2dp (source) too");
    check(profileFromUuid("00001124-0000-1000-8000-00805f9b34fb") == "hid", "1124 is hid");
    check(profileFromUuid("00001101-0000-1000-8000-00805f9b34fb") == "spp", "1101 is spp");
    check(profileFromUuid("00001134-0000-1000-8000-00805f9b34fb") == "mapc", "1134 is mapc");
    // The 16-bit id is read case-insensitively; BlueZ lowercases, but be safe.
    check(profileFromUuid("0000111E-0000-1000-8000-00805f9b34fb") == "hf", "the short id is read case-insensitively");
    // AVRCP (110e) is not one of HP's seven named profiles.
    check(profileFromUuid("0000110e-0000-1000-8000-00805f9b34fb") == "", "110e (AVRCP) maps to no HP profile");
    check(profileFromUuid("deadbeef-0000-1000-8000-00805f9b34fb") == "", "an unknown service maps to nothing");
    check(profileFromUuid("short") == "", "a string too short to hold a UUID is nothing");
}

static void testMenuProfilesOf()
{
    std::printf("menuProfilesOf: the menu profiles a device carries, for notifn* events\n");
    // The headset carries A2DP (110b) and Handsfree (111e): both are menu
    // profiles, so a connect/disconnect notification is keyed to both.
    const std::vector<std::string> hs = menuProfilesOf(headset());
    check(hs.size() == 2, "a headset carries two menu profiles");
    check(std::find(hs.begin(), hs.end(), "a2dp") != hs.end(), "a2dp is one");
    check(std::find(hs.begin(), hs.end(), "hf") != hs.end(), "hf is the other");
    // A HID-only device (a gamepad) carries no menu profile -- hid is in
    // hpProfiles but not menuProfiles, so no icon-moving event is keyed to it.
    Device gamepad;
    gamepad.uuids = { "00001124-0000-1000-8000-00805f9b34fb" };
    check(menuProfilesOf(gamepad).empty(), "a HID-only device carries no menu profile");
}

static void testRadioPayload()
{
    std::printf("btmonitor: the radio the status bar keys off\n");
    // The four strings StatusBarServicesConnector compares with strcmp.
    check(has(radioPayload(Radio::kOn, true), "\"radio\":\"on\""), "on carries radio:on");
    check(has(radioPayload(Radio::kOn, true), "\"notification\":\"notifnradioon\""), "on carries notifnradioon");
    check(has(radioPayload(Radio::kOff, true), "\"radio\":\"off\""), "off carries radio:off");
    check(has(radioPayload(Radio::kOff, true), "\"notification\":\"notifnradiooff\""), "off carries notifnradiooff");
    check(has(radioPayload(Radio::kTurningOn, true), "\"radio\":\"turningon\""), "turningon carries radio:turningon");
    check(has(radioPayload(Radio::kTurningOn, true), "notifnradioturningon"), "turningon carries notifnradioturningon");
    // There is no notifnradioturningoff in the connector; turning off carries
    // only the radio field.
    check(has(radioPayload(Radio::kTurningOff, true), "\"radio\":\"turningoff\""), "turningoff carries radio:turningoff");
    check(!has(radioPayload(Radio::kTurningOff, true), "\"notification\""), "turningoff carries no notification");
    // The subscribing reply must say so, as LS2 wants.
    check(has(radioPayload(Radio::kOn, true), "\"subscribed\":true"), "a subscribing reply says subscribed:true");
    check(has(radioPayload(Radio::kOn, false), "\"subscribed\":false"), "an unsubscribed reply says subscribed:false");
    // radioon/radiooff reply with returnValue; a failure keeps the message.
    check(radioResultPayload(true) == "{\"returnValue\":true}", "a radio success is returnValue:true");
    check(has(radioResultPayload(false, "rfkill blocked"), "\"returnValue\":false"), "a radio failure is returnValue:false");
    check(has(radioResultPayload(false, "rfkill blocked"), "rfkill blocked"), "a radio failure keeps the daemon's message");
}

static void testTrustedDevices()
{
    std::printf("gettrusteddevices: the four fields HP reads, plus the modern ones\n");
    BluetoothState state;
    state.adapter.present = true;
    state.adapter.powered = true;
    state.devices.push_back(headset());

    // An untrusted device in range must not appear in the trusted list.
    Device stranger;
    stranger.address = "00:11:22:33:44:55";
    stranger.name = "Someone's phone";
    stranger.trusted = false;
    state.devices.push_back(stranger);

    const std::string payload = trustedDevicesPayload(state);
    check(has(payload, "\"trusteddevices\":["), "the array HP iterates is named trusteddevices");
    check(has(payload, "\"address\":\"F8:DF:15:F2:29:ED\""), "the address is present");
    check(has(payload, "\"name\":\"AKG Y500 WIRELESS\""), "the name is present");
    check(has(payload, "\"status\":\"connected\""), "a connected device's status is connected");
    check(has(payload, "\"cod\":2360324"), "cod is the numeric Class HP reads");
    check(!has(payload, "Someone's phone"), "an untrusted device is left out");
    // Modern fields the card reads and the menu ignores.
    check(has(payload, "\"battery\":60"), "the battery percentage is carried for the card");
    check(has(payload, "\"icon\":\"audio-headset\""), "BlueZ's resolved icon is carried");
    check(has(payload, "\"addressType\":\"public\""), "the LE address type is carried");

    // A disconnected trusted device still lists, as disconnected.
    BluetoothState idle;
    Device off = headset();
    off.connected = false;
    off.battery = Device::kNoBattery;
    off.icon.clear();
    idle.devices.push_back(off);
    const std::string idlePayload = trustedDevicesPayload(idle);
    check(has(idlePayload, "\"status\":\"disconnected\""), "a disconnected trusted device reads disconnected");
    check(!has(idlePayload, "\"battery\""), "an absent battery is omitted, not sent as a sentinel");
    check(!has(idlePayload, "\"icon\""), "an absent icon is omitted");
}

static void testProfileState()
{
    std::printf("profgetstate: a connected device under each profile it carries\n");
    BluetoothState state;
    state.devices.push_back(headset());

    const std::string all = profileStatePayload(state, "all");
    // The headset carries a2dp and hf; it must appear under both.
    check(has(all, "\"a2dp\":[{\"state\":\"connected\",\"address\":\"F8:DF:15:F2:29:ED\""), "the headset is under a2dp");
    check(has(all, "\"hf\":[{\"state\":\"connected\""), "the headset is under hf");
    // It carries no HID/SPP/PAN/MAPC, so those arrays are empty, not missing.
    check(has(all, "\"hid\":[]"), "a profile the device lacks is an empty array");
    check(has(all, "\"spp\":[]"), "spp is empty for a headset");
    // All seven HP names must be present for profile:all, in HP's order.
    check(has(all, "\"hfg\":") && has(all, "\"a2dp\":") && has(all, "\"pan\":")
          && has(all, "\"hid\":") && has(all, "\"spp\":") && has(all, "\"hf\":")
          && has(all, "\"mapc\":"), "all seven HP profiles are present for profile:all");

    // A disconnected device is under no profile: profgetstate is connected
    // devices only (gettrusteddevices is the resting list).
    BluetoothState idle;
    Device off = headset();
    off.connected = false;
    idle.devices.push_back(off);
    const std::string idlePayload = profileStatePayload(idle, "all");
    check(has(idlePayload, "\"a2dp\":[]"), "a disconnected device is under no profile");

    // A single-profile request answers only that profile.
    const std::string just = profileStatePayload(state, "a2dp");
    check(has(just, "\"a2dp\":["), "a single-profile request answers that profile");
    check(!has(just, "\"hf\":"), "a single-profile request leaves the others out");
}

static void testProfileNotification()
{
    std::printf("prof/subscribenotifications: the transitions HP keys off\n");
    const std::string connecting = profileNotificationPayload(
        ProfileEvent::kConnecting, "a2dp", "F8:DF:15:F2:29:ED", "AKG Y500 WIRELESS");
    check(has(connecting, "\"notification\":\"notifnconnecting\""), "connecting is notifnconnecting");
    check(has(connecting, "\"profile\":\"a2dp\""), "the profile is carried");
    check(has(connecting, "\"address\":\"F8:DF:15:F2:29:ED\""), "the address is carried");
    check(has(connecting, "\"error\":0"), "a non-error notification carries error:0");

    const std::string connected = profileNotificationPayload(
        ProfileEvent::kConnected, "a2dp", "F8:DF:15:F2:29:ED", "AKG Y500 WIRELESS", 0);
    check(has(connected, "notifnconnected"), "connected is notifnconnected");

    // A failed connect: HP learns it from a non-zero error on notifnconnected.
    const std::string failed = profileNotificationPayload(
        ProfileEvent::kConnected, "a2dp", "F8:DF:15:F2:29:ED", "AKG Y500 WIRELESS", 1);
    check(has(failed, "\"error\":1"), "a failed connect carries a non-zero error");

    check(has(profileNotificationPayload(ProfileEvent::kDisconnected, "hf", "A", "B"), "notifndisconnected"),
          "disconnected is notifndisconnected");
    check(has(profileNotificationPayload(ProfileEvent::kDeviceRenamed, "", "A", "New name"), "notifndevrenamed"),
          "a rename is notifndevrenamed");
    check(has(profileNotificationPayload(ProfileEvent::kDeviceRemoved, "", "A", ""), "notifndevremoved"),
          "a removal is notifndevremoved");
}

static void testJsonEscape()
{
    std::printf("jsonEscape: a device name with a quote does not break the payload\n");
    BluetoothState state;
    Device d = headset();
    d.name = "Bob\"s \"Phone\"";   // a name with embedded quotes
    state.devices.push_back(d);
    const std::string payload = trustedDevicesPayload(state);
    check(!has(payload, "Bob\"s"), "a raw quote never reaches the payload unescaped");
    check(has(payload, "\\\"Phone\\\""), "the quote is escaped");
}

int main()
{
    testProfileFromUuid();
    testMenuProfilesOf();
    testRadioPayload();
    testTrustedDevices();
    testProfileState();
    testProfileNotification();
    testJsonEscape();

    if (g_failures == 0) {
        std::printf("\nall bluez-state checks passed\n");
        return 0;
    }
    std::printf("\n%d bluez-state check(s) failed\n", g_failures);
    return 1;
}
