// The per-network proxy store for getNwProxiesConfig / configureNwProxies.
//
// Mutation-verified: dropping the technology+scope key, treating auto-detect as
// a stored entry, accepting an unknown action, or omitting a required manual
// server each make it fail.
#include "network_proxies.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <cstdio>
#include <string>

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("  %-66s %s\n", what, ok ? "OK" : "<-- FAIL");
    if (!ok)
        ++g_failures;
}

int main()
{
    std::printf("configure\n");
    std::vector<NmNet::ProxyInfo> list;
    NmNet::ProxyInfo manual;
    manual.networkTechnology = "wifi";
    manual.proxyScope = "12";
    manual.proxyConfigType = NmNet::kProxyManual;
    manual.proxyServer = "proxy.example.com";
    manual.proxyPort = 8080;
    manual.hasPort = true;
    manual.isProxySecured = true;
    manual.hasSecured = true;

    check(NmNet::configureProxies(list, "add", manual).empty() && list.size() == 1,
          "add stores a manual proxy");
    check(list[0].proxyServer == "proxy.example.com" && list[0].proxyPort == 8080
              && list[0].isProxySecured,
          "with server, port and secured flag");

    NmNet::ProxyInfo other = manual;
    other.proxyScope = "13";
    other.proxyServer = "other.example.com";
    check(NmNet::configureProxies(list, "add", other).empty() && list.size() == 2,
          "a second scope is a second entry");

    NmNet::ProxyInfo update = manual;
    update.proxyServer = "new.example.com";
    update.proxyPort = 3128;
    check(NmNet::configureProxies(list, "add", update).empty() && list.size() == 2
              && list[0].proxyServer == "new.example.com" && list[0].proxyPort == 3128,
          "add with the same key replaces");

    NmNet::ProxyInfo pac;
    pac.networkTechnology = "wifi";
    pac.proxyScope = "12";
    pac.proxyConfigType = NmNet::kProxyPac;
    pac.proxyAutoConfigUrl = "http://wpad/proxy.pac";
    check(NmNet::configureProxies(list, "add", pac).empty() && list.size() == 2
              && list[0].proxyConfigType == NmNet::kProxyPac
              && list[0].proxyAutoConfigUrl == "http://wpad/proxy.pac",
          "PAC replaces a manual entry for that scope");

    const size_t beforeDetect = list.size();
    NmNet::ProxyInfo detect = pac;
    detect.proxyConfigType = NmNet::kProxyAutoDetect;
    detect.proxyAutoConfigUrl.clear();
    check(NmNet::configureProxies(list, "add", detect).empty() && list.size() == beforeDetect
              && list[0].proxyConfigType == NmNet::kProxyPac,
          "autoDetectFromNetwork succeeds without changing the store");

    NmNet::ProxyInfo clear = pac;
    clear.proxyConfigType = NmNet::kProxyNone;
    check(NmNet::configureProxies(list, "add", clear).empty() && list.size() == 1
              && list[0].proxyScope == "13",
          "noProxy removes that scope");
    check(NmNet::configureProxies(list, "rmv", other).empty() && list.empty(),
          "rmv removes by technology and scope");

    NmNet::ProxyInfo bad;
    bad.networkTechnology = "wifi";
    check(!NmNet::configureProxies(list, "add", bad).empty(), "scope is required");
    bad.proxyScope = "1";
    bad.proxyConfigType = NmNet::kProxyManual;
    check(!NmNet::configureProxies(list, "add", bad).empty(), "manual needs a server");
    bad.proxyConfigType = "somethingElse";
    bad.proxyServer = "x";
    check(!NmNet::configureProxies(list, "add", bad).empty(), "unknown types are refused");
    bad.proxyConfigType = NmNet::kProxyManual;
    check(!NmNet::configureProxies(list, "set", bad).empty(), "only add and rmv");

    std::printf("payload\n");
    list.clear();
    list.push_back(manual);
    const std::string payload = NmNet::proxiesConfigPayload(list);
    check(payload == "{\"returnValue\":true,\"subscribed\":false,\"proxyInfoList\":["
                     "{\"networkTechnology\":\"wifi\",\"proxyScope\":\"12\","
                     "\"proxyConfigType\":\"manualProxy\",\"proxyServer\":\"proxy.example.com\","
                     "\"proxyPort\":8080,\"isProxySecured\":true}]}",
          "getNwProxiesConfig shape");
    NmNet::ProxyInfo quoted = manual;
    quoted.proxyServer = "a\"b\\c";
    list[0] = quoted;
    check(NmNet::proxiesConfigPayload(list).find("\"proxyServer\":\"a\\\"b\\\\c\"") != std::string::npos,
          "quotes and backslashes in the server are escaped");

    std::printf("where it is\n");
    g_setenv("WEBOS_NETWORK_PROXIES", "/somewhere/proxies.json", TRUE);
    check(NetworkProxies::path() == "/somewhere/proxies.json", "WEBOS_NETWORK_PROXIES decides");
    g_unsetenv("WEBOS_NETWORK_PROXIES");
    const std::string fallback = std::string(g_get_user_data_dir()) + "/webos-ce/network-proxies.json";
    check(NetworkProxies::path() == fallback, "otherwise the user's data directory");

    std::printf("the file\n");
    gchar* dir = g_dir_make_tmp("network-proxies-XXXXXX", nullptr);
    const std::string file = std::string(dir) + "/network-proxies.json";
    check(NetworkProxies::load(file).empty(), "a missing file is an empty list");
    list.clear();
    list.push_back(manual);
    list.push_back(other);
    std::string error;
    check(NetworkProxies::save(file, list, error), "the list is written");
    const std::vector<NmNet::ProxyInfo> loaded = NetworkProxies::load(file);
    check(loaded.size() == 2 && loaded[0].proxyServer == "proxy.example.com"
              && loaded[1].proxyScope == "13",
          "and read back with the same keys");
    g_unlink(file.c_str());
    g_rmdir(dir);
    g_free(dir);

    std::printf("which one is active\n");
    list.clear();
    list.push_back(manual);
    list.push_back(other);
    check(NmNet::activeWifiProxy(list, 12) == &list[0]
              && NmNet::activeWifiProxy(list, 13) == &list[1],
          "the scope matches the joined wifi profileId");
    check(NmNet::activeWifiProxy(list, 0) == nullptr
              && NmNet::activeWifiProxy(list, 99) == nullptr,
          "idle radio and unknown scope apply nothing");
    const NmNet::AppProxy applied = NmNet::appProxyFor(list, 12);
    check(applied.kind == NmNet::AppProxy::Kind::Manual && applied.host == "proxy.example.com"
              && applied.port == 8080 && applied.secure,
          "manual becomes host:port for Qt");
    NmNet::ProxyInfo noPort = manual;
    noPort.hasPort = false;
    noPort.proxyPort = 0;
    list[0] = noPort;
    check(NmNet::appProxyFor(list, 12).port == 8080, "a missing port defaults to 8080");
    list.clear();
    list.push_back(pac);
    const NmNet::AppProxy pacApply = NmNet::appProxyFor(list, 12);
    check(pacApply.kind == NmNet::AppProxy::Kind::Pac
              && pacApply.pacUrl == "http://wpad/proxy.pac",
          "PAC becomes a pac URL for Chromium");
    check(NmNet::appProxyFor(list, 13).kind == NmNet::AppProxy::Kind::None,
          "a scope without a proxy clears the application proxy");

    std::printf("\n%s\n", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
