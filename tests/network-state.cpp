// What com.palm.connectionmanager reports, from a fake NetworkManager state.
//
// HP's pmnetconfigmanager-stub answered this call with a constant: connected,
// over wifi, on "Open webOS", always. Pull the cable or switch wifi off and
// nothing changed, so the email app kept trying to sync against a network that
// was not there.
//
// The payload is checked as strings rather than through a parser, for the same
// reason tests/power-state.cpp does it: every consumer reads named fields out of
// this object and silently drops the payload when one is missing, so a renamed
// field shows up as a network state that never changes -- not as an error.
//
// The first case here is the one that segfaults the shell rather than merely
// misreporting. StatusBarServicesConnector.cpp does, with no null check:
//     if(!strcmp(state, "connected") && !strcmp(onInternet, "yes"))
// on the raw pointers it pulled out of the "wifi" object.
//
// Measured on the machine this was written for, with NetworkManager running:
//     enp0s31f6  type=1  state=20   Wired.Carrier=false   (cable out)
//     wlp0s20f3  type=2  state=100  ssid=GachWLAN  strength=81  ip=192.168.1.66
//     tun0       type=16 state=100  (the VPN, and NM's PrimaryConnection)
//     Connectivity=4 (full)
#include "network_state.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("  %-66s %s\n", what, ok ? "OK" : "<-- FAIL");
    if (!ok)
        ++g_failures;
}

static bool has(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

// The shape the status bar dereferences. Asserted for every state below, since
// this is the one mistake that crashes rather than misreports.
static void checkNeverCrashesTheStatusBar(const std::string& payload, const char* whenn)
{
    char label[128];
    std::snprintf(label, sizeof label, "%s: wifi.state and wifi.onInternet are strings", whenn);
    const bool ok = has(payload, "\"wifi\":{\"state\":\"")
                    && has(payload, "\",\"onInternet\":\"");
    check(ok, label);
}

static NmNet::Device wifiDevice(int state, int strength, const char* ssid)
{
    NmNet::Device d;
    d.present = true;
    d.state = state;
    d.strength = strength;
    d.interfaceName = "wlp0s20f3";
    if (state == NmNet::kDeviceActivated) {
        d.ipAddress = "192.168.1.66";
        d.ssid = ssid;
    }
    return d;
}

static NmNet::Device wiredDevice(int state, bool carrier = true)
{
    NmNet::Device d;
    d.present = true;
    d.state = state;
    d.carrier = carrier;
    d.interfaceName = "enp0s31f6";
    if (state == NmNet::kDeviceActivated)
        d.ipAddress = "192.168.1.20";
    return d;
}

int main()
{
    // --- wifi up, internet reachable: the measured machine ------------------
    {
        NmNet::NetworkState s;
        s.connectivity = NmNet::kConnectivityFull;
        s.wifi = wifiDevice(NmNet::kDeviceActivated, 81, "GachWLAN");
        s.wired = wiredDevice(20);
        const std::string p = NmNet::statusPayload(s, true);

        std::printf("wifi connected, connectivity full\n");
        check(has(p, "\"isInternetConnectionAvailable\":true"), "the internet is available");
        check(has(p, "\"wifi\":{\"state\":\"connected\""), "wifi reads connected");
        check(has(p, "\"onInternet\":\"yes\""), "and on the internet");
        check(has(p, "\"ssid\":\"GachWLAN\""), "the real ssid is carried");
        check(has(p, "\"ipAddress\":\"192.168.1.66\""), "so is the real address");
        check(has(p, "\"networkConfidenceLevel\":\"excellent\""), "81% of signal is excellent");
        check(has(p, "\"wired\":{\"state\":\"disconnected\""), "the cable is out, and says so");
        check(has(p, "\"wan\":{\"state\":\"disconnected\"}"), "wan is always disconnected here");
        check(has(p, "\"subscribed\":true"), "the subscription is acknowledged");
        checkNeverCrashesTheStatusBar(p, "wifi up");
    }

    // --- a captive portal ---------------------------------------------------
    // The whole point of reading Connectivity rather than just the device: the
    // wifi is joined and has an address, and there is still no internet.
    {
        NmNet::NetworkState s;
        s.connectivity = NmNet::kConnectivityPortal;
        s.wifi = wifiDevice(NmNet::kDeviceActivated, 81, "AirportWiFi");
        const std::string p = NmNet::statusPayload(s, true);

        std::printf("\nbehind a captive portal\n");
        check(has(p, "\"isInternetConnectionAvailable\":false"), "the internet is NOT available");
        check(has(p, "\"wifi\":{\"state\":\"connected\""), "but the wifi is still connected");
        check(has(p, "\"onInternet\":\"no\""), "and known not to reach the internet");
        checkNeverCrashesTheStatusBar(p, "captive portal");
    }

    // --- no wifi at all, cable in -------------------------------------------
    // The case that crashes if "wifi" is omitted when there is no wifi device.
    {
        NmNet::NetworkState s;
        s.connectivity = NmNet::kConnectivityFull;
        s.wired = wiredDevice(NmNet::kDeviceActivated);
        const std::string p = NmNet::statusPayload(s, true);

        std::printf("\nno wifi device, cable in\n");
        check(has(p, "\"isInternetConnectionAvailable\":true"), "the internet is available");
        check(has(p, "\"wifi\":{\"state\":\"disconnected\""), "wifi reads disconnected");
        check(has(p, "\"wired\":{\"state\":\"connected\""), "the cable reads connected");
        check(has(p, "\"wifi\":{\"state\":\"disconnected\",\"onInternet\":\"no\""),
              "and the absent wifi does not claim the internet the cable provides");
        check(has(p, "\"ipAddress\":\"192.168.1.20\""), "with the wired address");
        check(!has(p, "\"ssid\""), "and no ssid is invented for it");
        checkNeverCrashesTheStatusBar(p, "no wifi device");
    }

    // --- nothing connected --------------------------------------------------
    {
        NmNet::NetworkState s;
        s.connectivity = NmNet::kConnectivityNone;
        s.wifi = wifiDevice(30, 0, "");
        s.wired = wiredDevice(20);
        const std::string p = NmNet::statusPayload(s, false);

        std::printf("\nnothing connected\n");
        check(has(p, "\"isInternetConnectionAvailable\":false"), "the internet is not available");
        check(has(p, "\"wifi\":{\"state\":\"disconnected\""), "wifi reads disconnected");
        check(has(p, "\"networkConfidenceLevel\":\"none\""), "and its confidence is none");
        check(has(p, "\"subscribed\":false"), "a non-subscribing call is told so");
        checkNeverCrashesTheStatusBar(p, "nothing connected");
    }

    // --- a VPN on top of wifi ------------------------------------------------
    // NM's PrimaryConnection is the tunnel in this state. What webOS is told is
    // the transport underneath, which is what it can actually act on.
    {
        NmNet::NetworkState s;
        s.connectivity = NmNet::kConnectivityFull;
        s.wifi = wifiDevice(NmNet::kDeviceActivated, 81, "GachWLAN");
        s.vpnActive = true;
        const std::string p = NmNet::statusPayload(s, true);

        std::printf("\nvpn up over wifi\n");
        check(has(p, "\"wifi\":{\"state\":\"connected\""), "the transport is still wifi");
        check(has(p, "\"ssid\":\"GachWLAN\""), "with the real network's name");
        check(has(p, "\"vpn\":{\"state\":\"connected\"}"), "and the vpn is reported as up");
        checkNeverCrashesTheStatusBar(p, "vpn up");
    }

    // --- the transport dropped a moment ago ---------------------------------
    // Measured live, switching wifi off with a subscriber watching: NM kept
    // reporting Connectivity FULL for about two seconds after the device had
    // gone, and the payload went out saying no wifi, no cable, and internet
    // available. luna-sysservice acts on that field.
    {
        NmNet::NetworkState s;
        s.connectivity = NmNet::kConnectivityFull;   // stale, as measured
        s.wifi = wifiDevice(30, 0, "GachWLAN");      // 30 = disconnected
        s.wired = wiredDevice(20);
        const std::string p = NmNet::statusPayload(s, true);

        std::printf("\nwifi just dropped, NM's Connectivity not caught up yet\n");
        check(has(p, "\"isInternetConnectionAvailable\":false"),
              "no transport means no internet, whatever NM still says");
        check(!has(p, "\"onInternet\":\"yes\""), "and nothing claims to be on it");
        checkNeverCrashesTheStatusBar(p, "transport just dropped");
    }

    // --- the cable, in or out --------------------------------------------
    // "disconnected" covers two states that are not the same thing, and only
    // one of them can be reconnected. Measured with the cable out: State 20,
    // Carrier false, and NetworkManager refusing a connect attempt with
    // "because device has no carrier". The system menu makes its row tappable
    // from this, so getting it wrong is a row that lies about what a tap does.
    {
        std::printf("\ncable in the socket, or not\n");

        NmNet::NetworkState out;
        out.wired = wiredDevice(20, false);
        check(has(NmNet::statusPayload(out, true), "\"carrier\":false"),
              "no cable says so");

        NmNet::NetworkState in;
        in.wired = wiredDevice(NmNet::kDeviceDisconnected, true);
        check(has(NmNet::statusPayload(in, true), "\"carrier\":true"),
              "a cable that is in, with the connection down, says so too");
        check(has(NmNet::statusPayload(in, true), "\"state\":\"disconnected\""),
              "and is still disconnected");

        NmNet::NetworkState up;
        up.connectivity = NmNet::kConnectivityFull;
        up.wired = wiredDevice(NmNet::kDeviceActivated, true);
        check(has(NmNet::statusPayload(up, true), "\"wired\":{\"state\":\"connected\"") 
              && has(NmNet::statusPayload(up, true), "\"carrier\":true"),
              "and a working cable is both");
    }

    // --- signal thresholds ---------------------------------------------------
    // activitymanager compares against exactly these four strings and treats
    // anything else as unknown, which silently unsets every *Confidence
    // requirement an activity declared.
    {
        std::printf("\nsignal strength to confidence\n");
        struct { int strength; const char* want; } cases[] = {
            { 100, "excellent" }, { 75, "excellent" }, { 74, "fair" },
            { 50, "fair" }, { 49, "poor" }, { 25, "poor" }, { 24, "none" },
            { 0, "none" },
        };
        for (const auto& c : cases) {
            NmNet::NetworkState s;
            s.connectivity = NmNet::kConnectivityFull;
            s.wifi = wifiDevice(NmNet::kDeviceActivated, c.strength, "GachWLAN");
            const std::string want = std::string("\"networkConfidenceLevel\":\"") + c.want + "\"";
            char label[128];
            std::snprintf(label, sizeof label, "%d%% of signal is %s", c.strength, c.want);
            check(has(NmNet::statusPayload(s, true), want), label);
        }
    }

    // --- an ssid that would break the payload --------------------------------
    // Access points name themselves whatever they like. An unescaped quote ends
    // the JSON string early and every consumer drops the entire object, so the
    // network would read as absent rather than as oddly named.
    {
        NmNet::NetworkState s;
        s.connectivity = NmNet::kConnectivityFull;
        s.wifi = wifiDevice(NmNet::kDeviceActivated, 81, "say \"hi\"\\there");
        const std::string p = NmNet::statusPayload(s, true);

        std::printf("\nan ssid containing a quote and a backslash\n");
        check(has(p, "\"ssid\":\"say \\\"hi\\\"\\\\there\""), "is escaped, so the payload stays JSON");
        check(!has(p, "\"ssid\":\"say \"hi\""), "and does not end the string early");

        NmNet::NetworkState c;
        c.connectivity = NmNet::kConnectivityFull;
        c.wifi = wifiDevice(NmNet::kDeviceActivated, 81, "tab\there");
        check(has(NmNet::statusPayload(c, true), "\"ssid\":\"tab\\there\""),
              "a control character is escaped too");
    }

    // --- com.palm.wifi, which is what actually moves the status bar's icon ----
    // The handler assigns m_wifiSSID = std::string(ssid) with no null check on
    // three of its branches, and indexes WIFI_BAR_1 + clamp(signalBars - 1, 1, 3)
    // into an enum that ends at WIFI_BAR_1 + 2. Both are pinned here.
    {
        std::printf("\ncom.palm.wifi status\n");

        NmNet::NetworkState none;                       // no wifi hardware
        check(has(NmNet::wifiStatusPayload(none, true), "\"status\":\"serviceDisabled\""),
              "no wifi device puts the icon out");

        NmNet::NetworkState idle;
        idle.wifi = wifiDevice(NmNet::kDeviceDisconnected, 0, "");
        check(has(NmNet::wifiStatusPayload(idle, true), "\"status\":\"serviceEnabled\""),
              "a radio joined to nothing leaves the icon on and empty");

        NmNet::NetworkState joining;
        joining.wifi = wifiDevice(NmNet::kDeviceConfig, 0, "");
        const std::string jp = NmNet::wifiStatusPayload(joining, true);
        check(has(jp, "\"connectState\":\"associating\""), "joining reads associating");
        check(has(jp, "\"ssid\":\""), "with an ssid that is a string, not absent");

        NmNet::NetworkState settling;
        settling.wifi = wifiDevice(NmNet::kDeviceIpConfig, 0, "");
        check(has(NmNet::wifiStatusPayload(settling, true), "\"connectState\":\"associated\""),
              "waiting for an address reads associated");

        NmNet::NetworkState failed;
        failed.wifi = wifiDevice(NmNet::kDeviceFailed, 0, "");
        check(has(NmNet::wifiStatusPayload(failed, true), "\"connectState\":\"associationFailed\""),
              "a failed association says so");

        NmNet::NetworkState up;
        up.connectivity = NmNet::kConnectivityFull;
        up.wifi = wifiDevice(NmNet::kDeviceActivated, 81, "GachWLAN");
        const std::string p = NmNet::wifiStatusPayload(up, true);
        check(has(p, "\"connectState\":\"ipConfigured\""), "connected reads ipConfigured");
        check(has(p, "\"ssid\":\"GachWLAN\""), "with the real network name");
        check(has(p, "\"signalBars\":3"), "and three bars at 81%");
        check(has(p, "\"signalLevel\":81"), "and the raw level beside them");
        check(has(p, "\"ipAddress\":\"192.168.1.66\""), "and the address");
    }

    // --- the icon enum must never be indexed past its end --------------------
    {
        std::printf("\nsignal bars stay inside HP's icon enum\n");
        bool everAboveThree = false;
        for (int strength = 0; strength <= 100; ++strength) {
            NmNet::Device d = wifiDevice(NmNet::kDeviceActivated, strength, "GachWLAN");
            const int bars = NmNet::signalBars(d.strength);
            if (bars > 3 || bars < 1)
                everAboveThree = true;
        }
        check(!everAboveThree, "every strength from 0 to 100 maps into 1..3");
        check(NmNet::signalBars(100) == 3 && NmNet::signalBars(0) == 1, "and spans the range");
    }

    // --- what counts as a wifi change ---------------------------------------
    // Measured live: the signal read 84, 80 then 79 within fifteen seconds, and
    // every one of them was a separate push to every subscriber for an icon that
    // has two distinguishable states.
    {
        std::printf("\nwhat is worth pushing to subscribers\n");
        NmNet::NetworkState a;
        a.connectivity = NmNet::kConnectivityFull;
        a.wifi = wifiDevice(NmNet::kDeviceActivated, 84, "GachWLAN");
        NmNet::NetworkState b = a;
        b.wifi.strength = 79;
        check(NmNet::wifiChangeKey(a) == NmNet::wifiChangeKey(b),
              "signal drifting within the same bar is not a change");
        check(NmNet::wifiStatusPayload(b, true).find("\"signalLevel\":79") != std::string::npos,
              "but the level still goes out when something else pushes");

        NmNet::NetworkState weak = a;
        weak.wifi.strength = 40;
        check(NmNet::wifiChangeKey(a) != NmNet::wifiChangeKey(weak), "a bar gained or lost is");

        NmNet::NetworkState renamed = a;
        renamed.wifi.ssid = "SomewhereElse";
        check(NmNet::wifiChangeKey(a) != NmNet::wifiChangeKey(renamed), "so is joining another network");

        NmNet::NetworkState moved = a;
        moved.wifi.ipAddress = "10.0.0.5";
        check(NmNet::wifiChangeKey(a) != NmNet::wifiChangeKey(moved), "so is a new address");

        NmNet::NetworkState dropped = a;
        dropped.wifi.state = NmNet::kDeviceDisconnected;
        check(NmNet::wifiChangeKey(a) != NmNet::wifiChangeKey(dropped), "so is dropping the network");
    }


    std::printf("\ncom.palm.wifi: the radio switched off\n");
    {
        NmNet::NetworkState off;
        off.wifi = wifiDevice(20, 0, "");
        off.wifiEnabled = false;
        check(has(NmNet::wifiStatusPayload(off, true), "\"status\":\"serviceDisabled\""),
              "a radio switched off reads serviceDisabled, not notAssociated");
        NmNet::NetworkState on = off;
        on.wifiEnabled = true;
        check(NmNet::wifiChangeKey(off) != NmNet::wifiChangeKey(on), "and switching it is a change");
    }

    std::printf("\ncom.palm.wifi: a failed join\n");
    {
        NmNet::NetworkState failed;
        failed.wifi = wifiDevice(NmNet::kDeviceFailed, 0, "");
        failed.wifiStateReason = NmNet::kReasonSupplicantDisconnect;
        failed.attemptedSsid = "Cafe";
        std::string p = NmNet::wifiStatusPayload(failed, true);
        check(has(p, "\"connectState\":\"associationFailed\""), "FAILED is associationFailed");
        check(has(p, "\"lastConnectError\":\"IncorrectPasskey\""), "a supplicant disconnect is a wrong password");
        check(has(p, "\"ssid\":\"Cafe\""), "naming the network that was being joined");

        NmNet::NetworkState settled = failed;
        settled.wifi.state = NmNet::kDeviceDisconnected;
        p = NmNet::wifiStatusPayload(settled, true);
        check(has(p, "\"connectState\":\"associationFailed\""),
              "still a failure once NM has settled on DISCONNECTED");
        check(NmNet::wifiChangeKey(settled) != NmNet::wifiChangeKey(NmNet::NetworkState()),
              "and a change worth pushing");

        NmNet::NetworkState userDisconnect = settled;
        userDisconnect.wifiStateReason = 39;   // USER_REQUESTED
        check(has(NmNet::wifiStatusPayload(userDisconnect, true), "\"status\":\"serviceEnabled\""),
              "a disconnect the user asked for is not a failure");

        NmNet::NetworkState nothingAttempted = settled;
        nothingAttempted.attemptedSsid.clear();
        check(has(NmNet::wifiStatusPayload(nothingAttempted, true), "\"status\":\"serviceEnabled\""),
              "nor is an old failure when nothing is being joined");

        check(std::string(NmNet::lastConnectError(NmNet::kReasonNoSecrets)) == "IncorrectPasskey",
              "NM asking for secrets again is a wrong password");
        check(std::string(NmNet::lastConnectError(NmNet::kReasonSupplicantTimeout)) == "IncorrectPasskey",
              "so is a handshake that timed out");
        check(std::string(NmNet::lastConnectError(NmNet::kReasonSsidNotFound)) == "ApNotFound",
              "a network that is not there is ApNotFound");
        check(std::string(NmNet::lastConnectError(1)) == "AssociationFailed", "anything else is generic");
    }

    std::printf("\ncom.palm.wifi: joining\n");
    {
        NmNet::NetworkState joining;
        joining.wifi = wifiDevice(NmNet::kDeviceConfig, 0, "");
        joining.attemptedSsid = "Cafe";
        joining.wifiProfileId = 12;
        const std::string p = NmNet::wifiStatusPayload(joining, true);
        check(has(p, "\"connectState\":\"associating\"") && has(p, "\"ssid\":\"Cafe\""),
              "associating names the network before NM knows the access point");
        check(has(p, "\"profileId\":12"), "and carries the profile being used");
    }

    std::printf("\nsecurity an access point advertises\n");
    {
        check(NmNet::apSecurity(0, 0, 0) == NmNet::kSecurityNone, "no flags is open");
        check(NmNet::apSecurity(NmNet::kApPrivacy, 0, 0) == NmNet::kSecurityWep, "privacy alone is WEP");
        check(NmNet::apSecurity(NmNet::kApPrivacy, 0, NmNet::kApKeyMgmtPsk) == NmNet::kSecurityWpaPsk,
              "RSN with PSK is WPA personal");
        check(NmNet::apSecurity(NmNet::kApPrivacy, NmNet::kApKeyMgmtPsk, 0) == NmNet::kSecurityWpaPsk,
              "and so is WPA1 with PSK");
        check(NmNet::apSecurity(NmNet::kApPrivacy, 0, NmNet::kApKeyMgmtSae) == NmNet::kSecuritySae,
              "SAE alone is WPA3");
        check(NmNet::apSecurity(NmNet::kApPrivacy, 0, NmNet::kApKeyMgmtSae | NmNet::kApKeyMgmtPsk)
                  == NmNet::kSecurityWpaPsk,
              "transition mode is joined with PSK");
        check(NmNet::apSecurity(NmNet::kApPrivacy, 0, NmNet::kApKeyMgmt8021x | NmNet::kApKeyMgmtPsk)
                  == NmNet::kSecurityEnterprise,
              "802.1X is enterprise whatever else is offered");
        check(NmNet::securityType(NmNet::kSecurityNone) == nullptr, "an open network has no securityType");
        check(std::string(NmNet::securityType(NmNet::kSecuritySae)) == "wpa-personal",
              "WPA3 is wpa-personal to webOS");
        check(std::string(NmNet::keyManagement(NmNet::kSecuritySae)) == "sae", "but sae to NetworkManager");
        check(std::string(NmNet::keyManagement(NmNet::kSecurityWep)) == "none", "WEP is key-mgmt none");
    }

    std::printf("\nprofile ids\n");
    {
        check(NmNet::profileIdOf("/org/freedesktop/NetworkManager/Settings/12") == 12, "the number ends the path");
        check(NmNet::profileIdOf("/org/freedesktop/NetworkManager/Devices/12") == 0, "only a settings path");
        check(NmNet::profileIdOf("/org/freedesktop/NetworkManager/Settings/") == 0, "with a number");
        check(NmNet::profileIdOf("/org/freedesktop/NetworkManager/Settings/1x") == 0, "and nothing else");
        check(NmNet::profileIdOf("/org/freedesktop/NetworkManager/Settings/99999999999") == 0,
              "that fits in an int");
        check(NmNet::settingsPathOf(7) == "/org/freedesktop/NetworkManager/Settings/7", "and back");
    }

    std::printf("\nthe scan list\n");
    {
        std::vector<NmNet::AccessPoint> raw(5);
        raw[0].ssid = "Office"; raw[0].strength = 40;
        raw[1].ssid = "Office"; raw[1].strength = 90; raw[1].security = NmNet::kSecurityWpaPsk;
        raw[2].ssid = "";       raw[2].strength = 99;
        raw[3].ssid = "Home";   raw[3].strength = 30; raw[3].active = true; raw[3].profileId = 4;
        raw[4].ssid = "Office"; raw[4].strength = 10; raw[4].profileId = 9;
        raw[0].security = NmNet::kSecurityWpaPsk;
        const std::vector<NmNet::AccessPoint> merged = NmNet::mergeScan(raw);
        check(merged.size() == 2, "one entry per name, hidden networks left out");
        check(merged.size() == 2 && merged[0].ssid == "Home", "the joined network first, however weak");
        check(merged.size() == 2 && merged[1].strength == 90, "the strongest access point speaks for the name");
        check(merged.size() == 2 && merged[1].profileId == 9, "keeping a profile any of them had");

        NmNet::NetworkState joined;
        joined.wifi = wifiDevice(NmNet::kDeviceActivated, 30, "Home");
        const std::string p = NmNet::foundNetworksPayload(merged, joined);
        check(has(p, "{\"networkInfo\":{\"ssid\":\"Home\",\"signalBars\":1,\"signalLevel\":30,\"profileId\":4,\"connectState\":\"ipConfigured\"}}"),
              "the joined network: no securityType, its profile and its state");
        check(has(p, "{\"networkInfo\":{\"ssid\":\"Office\",\"signalBars\":3,\"signalLevel\":90,\"securityType\":\"wpa-personal\",\"profileId\":9}}"),
              "another: its security, no connectState");
        check(NmNet::foundNetworksPayload({}, joined) == "{\"returnValue\":true,\"foundNetworks\":[]}",
              "an empty scan is an empty list, not an error");
    }

    std::printf("\nprofile, info and errors\n");
    {
        NmNet::Profile profile;
        profile.profileId = 4;
        profile.ssid = "Ho\"me";
        profile.security = NmNet::kSecurityWep;
        NmNet::IpInfo ip;
        ip.ip = "192.168.1.66"; ip.subnet = NmNet::subnetMask(24); ip.gateway = "192.168.1.1";
        ip.dns1 = "1.1.1.1";
        const std::string p = NmNet::profilePayload(profile, &ip);
        check(has(p, "\"wifiProfile\":{\"profileId\":4,\"ssid\":\"Ho\\\"me\",\"securityType\":\"wep\",\"useStaticIp\":false}"),
              "the profile, its name escaped");
        check(has(p, "\"ipInfo\":{\"ip\":\"192.168.1.66\",\"subnet\":\"255.255.255.0\",\"gateway\":\"192.168.1.1\",\"dns1\":\"1.1.1.1\"}"),
              "the address at the top level, where the library reads it");
        check(!has(NmNet::profilePayload(profile, nullptr), "ipInfo"), "no address for a profile not in use");
        check(NmNet::subnetMask(0) == "0.0.0.0" && NmNet::subnetMask(32) == "255.255.255.255"
                  && NmNet::subnetMask(20) == "255.255.240.0" && NmNet::subnetMask(33).empty(),
              "prefix lengths as masks");
        check(NmNet::infoPayload("AA:BB") == "{\"returnValue\":true,\"wifiInfo\":{\"macAddress\":\"AA:BB\",\"wapi\":\"disabled\"}}",
              "getinfo, with WAPI off");
        check(NmNet::errorPayload("bad \"x\"") == "{\"returnValue\":false,\"errorText\":\"bad \\\"x\\\"\"}",
              "errors are JSON too");
    }

    std::printf("\nleaving a network\n");
    {
        NmNet::NetworkState casa;
        casa.wifi = wifiDevice(NmNet::kDeviceActivated, 80, "Casa");
        check(NmNet::joinedSsid(casa) == "Casa", "the joined network is named");
        NmNet::NetworkState joining;
        joining.wifi = wifiDevice(NmNet::kDeviceConfig, 0, "");
        joining.attemptedSsid = "Oficina";
        check(NmNet::joinedSsid(joining).empty(), "a network being joined is not joined yet");
        check(NmNet::leftNetworkPayload("Casa", joining)
                  == "{\"returnValue\":true,\"subscribed\":true,\"status\":\"connectionStateChanged\","
                     "\"networkInfo\":{\"connectState\":\"notAssociated\",\"ssid\":\"Casa\"}}",
              "moving on from Casa says Casa was left");
        check(NmNet::leftNetworkPayload("Casa", casa).empty(), "staying on Casa says nothing");
        check(NmNet::leftNetworkPayload("", joining).empty(), "nothing joined before, nothing left");
        NmNet::NetworkState off = casa;
        off.wifiEnabled = false;
        check(!NmNet::leftNetworkPayload("Casa", off).empty(), "the radio going off leaves it too");
        NmNet::NetworkState other;
        other.wifi = wifiDevice(NmNet::kDeviceActivated, 60, "Ofi\"cina");
        check(has(NmNet::leftNetworkPayload("Ca\"sa", other), "\"ssid\":\"Ca\\\"sa\""),
              "the left network's name escaped");
    }

    std::printf("\nthe access point, for the settings card\n");
    {
        NmNet::NetworkState joined;
        joined.wifi = wifiDevice(NmNet::kDeviceActivated, 80, "Casa");
        joined.wifiBssid = "AA:BB:CC:DD:EE:FF";
        joined.wifiFrequency = 5180;
        const std::string p = NmNet::wifiStatusPayload(joined, true);
        check(has(p, "},\"apInfo\":{\"bssid\":\"AA:BB:CC:DD:EE:FF\",\"channel\":36}}"),
              "apInfo beside networkInfo, with the channel");
        NmNet::NetworkState roamed = joined;
        roamed.wifiBssid = "AA:BB:CC:DD:EE:00";
        check(NmNet::wifiChangeKey(joined) != NmNet::wifiChangeKey(roamed), "roaming is a change");
        NmNet::NetworkState joining = joined;
        joining.wifi.state = NmNet::kDeviceConfig;
        check(!has(NmNet::wifiStatusPayload(joining, true), "apInfo"), "no apInfo before the network is up");
        check(NmNet::channelOf(2412) == 1 && NmNet::channelOf(2472) == 13 && NmNet::channelOf(2484) == 14,
              "2.4 GHz channels");
        check(NmNet::channelOf(5180) == 36 && NmNet::channelOf(5825) == 165, "5 GHz channels");
        check(NmNet::channelOf(5955) == 1 && NmNet::channelOf(6115) == 33, "6 GHz channels");
        check(NmNet::channelOf(900) == 0, "anything else is 0");
    }

    std::printf("\nthe saved networks\n");
    {
        std::vector<NmNet::Profile> list(2);
        list[0].profileId = 3; list[0].ssid = "Casa";
        list[1].profileId = 11; list[1].ssid = "Ofi\"cina"; list[1].security = NmNet::kSecuritySae;
        check(NmNet::profileListPayload(list)
                  == "{\"returnValue\":true,\"profileList\":[{\"wifiProfile\":{\"profileId\":3,\"ssid\":\"Casa\"}},"
                     "{\"wifiProfile\":{\"profileId\":11,\"ssid\":\"Ofi\\\"cina\",\"security\":{\"securityType\":\"wpa-personal\"}}}]}",
              "HP's shape: security nested, absent for an open network");
        check(NmNet::profileListPayload({}) == "{\"returnValue\":true,\"profileList\":[]}", "none saved is an empty list");
    }


    std::printf("\nenterprise\n");
    {
        NmNet::ConnectRequest r;
        r.ssid = "Corp"; r.securityType = "enterprise"; r.eapType = "eapPeap";
        r.userId = "gach"; r.password = "secret";
        check(NmNet::validateConnect(r).empty(), "PEAP with a user and a password is accepted");
        r.password.clear();
        check(!NmNet::validateConnect(r).empty(), "without the password it is not");
        r.password = "secret"; r.userId.clear();
        check(!NmNet::validateConnect(r).empty(), "nor without the user");
        r = NmNet::ConnectRequest(); r.ssid = "Corp"; r.securityType = "enterprise";
        r.eapType = "eapTls"; r.userId = "gach";
        check(!NmNet::validateConnect(r).empty(), "TLS without a certificate is refused");
        r.clientCertificatePath = "/certs/me.pem";
        check(NmNet::validateConnect(r).empty(), "TLS with one needs no password");
        r.eapType = "eapLeap";
        check(!NmNet::validateConnect(r).empty(), "an EAP type the card does not offer is refused");
        r.eapType = "";
        r.password = "x";
        check(NmNet::validateConnect(r).empty(), "no EAP type is Auto");

        check(NmNet::eapMethods("eapAuto") == std::vector<std::string>({"peap", "ttls"}), "Auto offers PEAP and TTLS");
        check(NmNet::eapMethods("eapPeap") == std::vector<std::string>({"peap"}), "PEAP");
        check(NmNet::eapMethods("eapTtls") == std::vector<std::string>({"ttls"}), "TTLS");
        check(NmNet::eapMethods("eapTls") == std::vector<std::string>({"tls"}), "TLS");
        check(NmNet::eapMethods("eapFast") == std::vector<std::string>({"fast"}), "FAST");
        check(NmNet::eapMethods("eapLeap").empty(), "and nothing else");

        NmNet::ConnectRequest e; e.securityType = "enterprise";
        check(NmNet::requestedSecurity(e, NmNet::kSecurityNone) == NmNet::kSecurityEnterprise,
              "an enterprise request is joined as enterprise");
        check(std::string(NmNet::keyManagement(NmNet::kSecurityEnterprise)) == "wpa-eap", "with wpa-eap");
        check(std::string(NmNet::lastConnectError(NmNet::kReasonSupplicantDisconnect, true)) == "IncorrectPassword",
              "a rejected enterprise login is the user name or password");

        NmNet::NetworkState failed;
        failed.wifi = wifiDevice(NmNet::kDeviceFailed, 0, "");
        failed.wifiStateReason = NmNet::kReasonNoSecrets;
        failed.attemptedSsid = "Corp";
        failed.attemptedEnterprise = true;
        check(has(NmNet::wifiStatusPayload(failed, true), "\"lastConnectError\":\"IncorrectPassword\""),
              "and is reported so");
    }

    std::printf("\naddress settings\n");
    {
        unsigned long v = 0;
        check(NmNet::parseIpv4("192.168.1.20", v) && v == 0xC0A80114UL, "an address is read");
        check(!NmNet::parseIpv4("192.168.1", v), "three parts are not an address");
        check(!NmNet::parseIpv4("192.168.1.256", v), "nor is an octet past 255");
        check(!NmNet::parseIpv4("192.168..1", v) && !NmNet::parseIpv4("a.b.c.d", v)
                  && !NmNet::parseIpv4("1.2.3.4.5", v) && !NmNet::parseIpv4("", v),
              "nor anything else");
        check(NmNet::prefixOfMask("255.255.255.0") == 24 && NmNet::prefixOfMask("255.255.240.0") == 20
                  && NmNet::prefixOfMask("255.255.255.255") == 32 && NmNet::prefixOfMask("128.0.0.0") == 1,
              "masks are prefix lengths");
        check(NmNet::prefixOfMask("255.0.255.0") == -1, "a mask with a hole is not one");
        check(NmNet::prefixOfMask("0.0.0.0") == -1, "and neither is no mask");

        NmNet::ConnectRequest r;
        r.addressChange = true;
        check(!NmNet::validateConnect(r).empty(), "address settings need a saved network");
        r.profileId = 3;
        check(NmNet::validateConnect(r).empty(), "going back to DHCP needs nothing else");
        r.staticIp = true; r.ip = "192.168.1.20"; r.subnet = "255.255.255.0";
        check(NmNet::validateConnect(r).empty(), "an address and a mask are enough");
        r.gateway = "192.168.1.1"; r.dns1 = "1.1.1.1"; r.dns2 = "9.9.9.9";
        check(NmNet::validateConnect(r).empty(), "with a gateway and two DNS servers");
        r.ip = "192.168.1";
        check(has(NmNet::validateConnect(r), "IP address"), "a bad address is named");
        r.ip = "192.168.1.20"; r.subnet = "255.0.255.0";
        check(has(NmNet::validateConnect(r), "subnet"), "so is a bad mask");
        r.subnet = "255.255.255.0"; r.gateway = "x";
        check(has(NmNet::validateConnect(r), "gateway"), "and a bad gateway");
        r.gateway = ""; r.dns2 = "300.1.1.1";
        check(has(NmNet::validateConnect(r), "DNS"), "and a bad DNS server");
    }

    std::printf("\ncertificates\n");
    {
        check(NmNet::dnField("CN=Laptop,O=Example Corp,C=US", "CN") == "Laptop", "the common name");
        check(NmNet::dnField("CN=Laptop, O=Example Corp", "O") == "Example Corp", "the organization, after a space");
        check(NmNet::dnField("O=Acme\\, Inc.,CN=Me", "O") == "Acme, Inc.", "an escaped comma is part of the value");
        check(NmNet::dnField("O=Acme", "CN").empty(), "a missing field is empty");
        std::vector<NmNet::Certificate> list(2);
        list[0].certificateId = 1; list[0].commonName = "Me"; list[0].path = "/c/me.pem";
        list[1].certificateId = 2; list[1].organization = "Acme \"Corp\""; list[1].path = "/c/acme.pem";
        check(NmNet::certificateListPayload(list)
                  == "{\"returnValue\":true,\"userCertificateStore\":["
                     "{\"certificateId\":1,\"commonname\":\"Me\",\"certificateFilename\":\"/c/me.pem\"},"
                     "{\"certificateId\":2,\"organization\":\"Acme \\\"Corp\\\"\",\"certificateFilename\":\"/c/acme.pem\"}]}",
              "the store in the shape the library reads");
    }

    std::printf("\nwhen the device sleeps\n");
    {
        bool turnedOff = false;
        check(NmNet::sleepRadioAction(true, true, true, turnedOff) == NmNet::SleepRadio::Nothing && !turnedOff,
              "Keep Wi-Fi On leaves the radio alone going to sleep");
        check(NmNet::sleepRadioAction(true, false, true, turnedOff) == NmNet::SleepRadio::Nothing,
              "and waking up");
        check(NmNet::sleepRadioAction(false, true, true, turnedOff) == NmNet::SleepRadio::TurnOff && turnedOff,
              "Turn Wi-Fi Off switches it off going to sleep");
        check(NmNet::sleepRadioAction(false, false, false, turnedOff) == NmNet::SleepRadio::TurnOn && !turnedOff,
              "and back on waking up");
        check(NmNet::sleepRadioAction(false, false, false, turnedOff) == NmNet::SleepRadio::Nothing,
              "only once");
        check(NmNet::sleepRadioAction(false, true, false, turnedOff) == NmNet::SleepRadio::Nothing && !turnedOff,
              "a radio already off is not touched going to sleep");
        check(NmNet::sleepRadioAction(false, false, false, turnedOff) == NmNet::SleepRadio::Nothing,
              "so it stays off on waking up");
        turnedOff = false;
        NmNet::sleepRadioAction(false, true, true, turnedOff);
        check(NmNet::sleepRadioAction(true, false, false, turnedOff) == NmNet::SleepRadio::TurnOn,
              "switching to Keep Wi-Fi On while asleep still restores the radio");

        bool keep = true;
        check(NmNet::parseWakeOnWifiMode("disable", keep) && !keep, "disable is read");
        check(NmNet::parseWakeOnWifiMode("enable", keep) && keep, "enable is read");
        check(!NmNet::parseWakeOnWifiMode("off", keep) && keep, "anything else is refused, leaving the mode");
        check(NmNet::wakeOnWifiPayload(false) == "{\"returnValue\":true,\"mode\":\"disable\"}",
              "the mode is answered as HP's card reads it");
    }

    std::printf("\nwhat connect accepts\n");
    {
        NmNet::ConnectRequest r;
        check(!NmNet::validateConnect(r).empty(), "nothing to connect to is refused");
        r.profileId = 3;
        check(NmNet::validateConnect(r).empty(), "a profile is enough");
        r.profileId = -1;
        check(!NmNet::validateConnect(r).empty(), "a negative profile is not");
        r = NmNet::ConnectRequest(); r.ssid = "Cafe";
        check(NmNet::validateConnect(r).empty(), "an open network needs only its name");
        r.ssid = std::string(33, 'x');
        check(!NmNet::validateConnect(r).empty(), "a name longer than 32 bytes is refused");
        r = NmNet::ConnectRequest(); r.ssid = "Home"; r.securityType = "wpa-personal";
        r.passKey = "1234567";
        check(!NmNet::validateConnect(r).empty(), "a 7-character WPA password is refused");
        r.passKey = "12345678";
        check(NmNet::validateConnect(r).empty(), "8 is accepted");
        r.passKey = std::string(63, 'a');
        check(NmNet::validateConnect(r).empty(), "63 is accepted");
        r.passKey = std::string(64, 'a');
        check(NmNet::validateConnect(r).empty(), "64 hex digits are a raw key");
        r.passKey = std::string(64, 'z');
        check(!NmNet::validateConnect(r).empty(), "64 characters that are not hex are not");
        r = NmNet::ConnectRequest(); r.ssid = "Old"; r.securityType = "wep";
        r.passKey = "abcde";
        check(NmNet::validateConnect(r).empty(), "a 5-character WEP key");
        r.passKey = "0123456789";
        check(NmNet::validateConnect(r).empty(), "a 10-digit hex WEP key");
        r.passKey = "abcdef";
        check(!NmNet::validateConnect(r).empty(), "a 6-character WEP key is refused");
        r.passKey = "abcde"; r.keyIndex = 4;
        check(!NmNet::validateConnect(r).empty(), "and a key index past 3");
        r = NmNet::ConnectRequest(); r.ssid = "Corp"; r.securityType = "wapi-psk";
        check(!NmNet::validateConnect(r).empty(), "WAPI is refused");

        NmNet::ConnectRequest wpa; wpa.securityType = "wpa-personal";
        check(NmNet::requestedSecurity(wpa, NmNet::kSecuritySae) == NmNet::kSecuritySae,
              "a password for a WPA3-only network is joined with SAE");
        check(NmNet::requestedSecurity(wpa, NmNet::kSecurityWpaPsk) == NmNet::kSecurityWpaPsk,
              "otherwise with PSK");
        check(NmNet::requestedSecurity(wpa, NmNet::kSecurityNone) == NmNet::kSecurityWpaPsk,
              "including when the network was not in the scan");
    }

    std::printf("vpn payloads\n");
    {
        NmNet::VpnProfile p;
        p.name = "Work";
        p.connectState = NmNet::kVpnStateDisconnected;
        p.agentGuid = NmNet::kVpnAgentOpenVpn;
        const std::string item = NmNet::vpnProfileItem(p);
        check(has(item, "\"vpnProfileName\":\"Work\""), "list item names the profile");
        check(has(item, "\"vpnProfileConnectState\":\"disconnected\""), "and its state");
        check(has(item, "\"vpnAgentGuid\":\"com.gachlab.openvpn\""), "and our agent id");
        check(item.size() < 255, "and stays under the status bar buffer");
        check(!has(item, "password") && !has(item, "remote"),
              "secrets and remotes stay off the list item");

        const std::string list = NmNet::vpnProfileListPayload({ p }, true);
        check(has(list, "\"returnValue\":true") && has(list, "\"subscribed\":true")
                  && has(list, "\"vpnProfiles\":["),
              "getProfileList wraps the items");
        check(has(NmNet::vpnStatusPayload(true, false), "\"connected\":true")
                  && has(NmNet::vpnStatusPayload(true, false), "\"subscribed\":false"),
              "getStatus reports connected");

        const auto agents = NmNet::builtInVpnAgents();
        check(agents.size() == 2 && agents[0].guid == NmNet::kVpnAgentOpenVpn
                  && agents[1].guid == NmNet::kVpnAgentWireGuard,
              "the two agents this port ships");
        check(has(NmNet::vpnAgentsPayload(agents), "OpenVPN")
                  && has(NmNet::vpnAgentsPayload(agents), "WireGuard"),
              "getAgents names them");
        check(NmNet::knownVpnAgent(NmNet::kVpnAgentOpenVpn)
                  && !NmNet::knownVpnAgent("com.palm.vpnc"),
              "only our agents are accepted for add/update");
    }

    std::printf("\n%s\n", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
