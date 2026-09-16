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

static NmNet::Device wiredDevice(int state)
{
    NmNet::Device d;
    d.present = true;
    d.state = state;
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

    std::printf("\n%s\n", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
