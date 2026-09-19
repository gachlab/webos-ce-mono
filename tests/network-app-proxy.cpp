// WebAppMgr applies the active wifi proxy as Qt's application proxy (#23).
//
// Mutation-verified: skipping consider, ignoring profileId, or leaving a stale
// manual host after None each make it fail.

#include "NetworkAppProxyAdapter.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkProxy>

#include <cstdio>
#include <vector>

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("  %-66s %s\n", what, ok ? "OK" : "<-- FAIL");
    if (!ok)
        ++g_failures;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    std::vector<NmNet::AppProxy> applied;
    NetworkAppProxy::reset();
    NetworkAppProxy::setApply([&applied](const NmNet::AppProxy& proxy) { applied.push_back(proxy); });

    std::printf("consider\n");
    std::vector<NmNet::ProxyInfo> list;
    NmNet::ProxyInfo manual;
    manual.networkTechnology = "wifi";
    manual.proxyScope = "7";
    manual.proxyConfigType = NmNet::kProxyManual;
    manual.proxyServer = "proxy.lan";
    manual.proxyPort = 3128;
    manual.hasPort = true;
    list.push_back(manual);

    NetworkAppProxy::consider(0, list);
    check(applied.empty(), "no wifi profile leaves the application proxy alone");

    NetworkAppProxy::consider(7, list);
    check(applied.size() == 1 && applied[0].kind == NmNet::AppProxy::Kind::Manual
              && applied[0].host == "proxy.lan" && applied[0].port == 3128,
          "the joined profile's manual proxy is applied");

    NetworkAppProxy::consider(7, list);
    check(applied.size() == 1, "the same proxy is not applied twice");

    NetworkAppProxy::consider(8, list);
    check(applied.size() == 2 && applied[1].kind == NmNet::AppProxy::Kind::None,
          "leaving that network clears the proxy");

    std::printf("json\n");
    const QJsonObject status{{"wifi", QJsonObject{{"state", "connected"}, {"profileId", 7}}}};
    check(NetworkAppProxy::wifiProfileIdOf(status) == 7, "getstatus wifi.profileId is read");
    check(NetworkAppProxy::wifiProfileIdOf(QJsonObject{{"wifi", QJsonObject{{"state", "disconnected"},
                                                                            {"profileId", 7}}}})
              == 0,
          "a disconnected radio has no active scope");

    const QJsonObject reply{
        {"proxyInfoList",
         QJsonArray{QJsonObject{{"networkTechnology", "wifi"},
                                {"proxyScope", "7"},
                                {"proxyConfigType", "manualProxy"},
                                {"proxyServer", "p.example"},
                                {"proxyPort", 8080}}}}};
    const auto parsed = NetworkAppProxy::proxiesOf(reply);
    check(parsed.size() == 1 && parsed[0].proxyServer == "p.example" && parsed[0].proxyPort == 8080,
          "getNwProxiesConfig list is read");

    std::printf("qt\n");
    NetworkAppProxy::setApply(nullptr);
    NetworkAppProxy::reset();
    NmNet::AppProxy manualApply;
    manualApply.kind = NmNet::AppProxy::Kind::Manual;
    manualApply.host = "127.0.0.1";
    manualApply.port = 8888;
    NetworkAppProxy::applyWithQt(manualApply);
    const QNetworkProxy current = QNetworkProxy::applicationProxy();
    check(current.type() == QNetworkProxy::HttpProxy && current.hostName() == "127.0.0.1"
              && current.port() == 8888,
          "applyWithQt sets QNetworkProxy::applicationProxy");
    NetworkAppProxy::applyWithQt(NmNet::AppProxy{});
    check(QNetworkProxy::applicationProxy().type() == QNetworkProxy::NoProxy,
          "and None clears it");

    std::printf("\n%s\n", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
