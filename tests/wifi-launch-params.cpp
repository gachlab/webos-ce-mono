// What the system menu opens the Wi-Fi card with.
//
// HP's sprintf ended in a trailing comma, {"target": {...},}, which JSON.parse
// refuses; enyo then dropped the parameters and the card opened on its list
// instead of on the network the user tapped. The name also went in unescaped.
#include "WifiLaunchParams.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("  %-66s %s\n", what, ok ? "OK" : "<-- FAIL");
    if (!ok)
        ++g_failures;
}

static QJsonObject targetOf(const std::string& params, bool& valid)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(params), &error);
    valid = error.error == QJsonParseError::NoError && doc.isObject();
    return doc.object().value(QStringLiteral("target")).toObject();
}

int main()
{
    bool valid = false;

    std::printf("a secured network with no profile\n");
    QJsonObject t = targetOf(wifiLaunchParams(QStringLiteral("Oficina"), QStringLiteral("wpa-personal"), 0, QString()), valid);
    check(valid, "is valid JSON");
    check(t.value(QStringLiteral("ssid")).toString() == QStringLiteral("Oficina"), "names the network");
    check(t.value(QStringLiteral("securityType")).toString() == QStringLiteral("wpa-personal"), "with its security");
    check(!t.contains(QStringLiteral("profileId")) && !t.contains(QStringLiteral("connectState")),
          "and no profile or state, which is what opens the join screen");

    std::printf("the joined network\n");
    t = targetOf(wifiLaunchParams(QStringLiteral("Casa"), QString(), 10, QStringLiteral("ipConfigured")), valid);
    check(valid, "is valid JSON");
    check(t.value(QStringLiteral("profileId")).toInt() == 10, "carries its profile");
    check(t.value(QStringLiteral("connectState")).toString() == QStringLiteral("ipConfigured"), "and its state");

    std::printf("a name that is not polite\n");
    const QString odd = QString::fromUtf8("Caf\xc3\xa9 \"5G\" \\ {x}");
    t = targetOf(wifiLaunchParams(odd, QStringLiteral("wep"), 0, QString()), valid);
    check(valid, "quotes, a backslash and braces still make valid JSON");
    check(t.value(QStringLiteral("ssid")).toString() == odd, "and the name comes back as it was");

    std::printf("\n%s\n", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
