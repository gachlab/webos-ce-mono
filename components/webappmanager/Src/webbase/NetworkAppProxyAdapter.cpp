// Per-network proxies → QNetworkProxy. Apart from the bus so its tests need
// no luna-service2.

#include "NetworkAppProxyAdapter.h"

#include <QJsonArray>
#include <QNetworkProxy>

namespace NetworkAppProxy {

namespace {

Apply& applySlot()
{
    static Apply apply = applyWithQt;
    return apply;
}

NmNet::AppProxy g_last;

// Chromium reads QTWEBENGINE_CHROMIUM_FLAGS at engine start. Keep GPU defaults
// from qtwebkit-compat (#84) and replace only our proxy flags.
void setChromiumProxyFlags(const NmNet::AppProxy& proxy)
{
    QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    QList<QByteArray> kept;
    for (const QByteArray& part : flags.split(' ')) {
        if (part.isEmpty())
            continue;
        if (part.startsWith("--proxy-server=") || part.startsWith("--proxy-pac-url="))
            continue;
        kept.append(part);
    }
    if (proxy.kind == NmNet::AppProxy::Kind::Manual) {
        QByteArray server = proxy.secure ? "https://" : "";
        server += QByteArray::fromStdString(proxy.host) + ':' + QByteArray::number(proxy.port);
        kept.append("--proxy-server=" + server);
    } else if (proxy.kind == NmNet::AppProxy::Kind::Pac) {
        kept.append("--proxy-pac-url=" + QByteArray::fromStdString(proxy.pacUrl));
    }
    // Never qputenv("") — that drops GPU defaults after Chromium has already
    // read the flags (and can race the qtwebkit-compat beforeApplication set).
    const QByteArray next = kept.join(' ');
    if (!next.isEmpty())
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", next);
}

} // namespace

void applyWithQt(const NmNet::AppProxy& proxy)
{
    setChromiumProxyFlags(proxy);
    if (proxy.kind == NmNet::AppProxy::Kind::Manual) {
        QNetworkProxy qt(QNetworkProxy::HttpProxy, QString::fromStdString(proxy.host),
                         quint16(proxy.port > 0 ? proxy.port : 8080));
        QNetworkProxy::setApplicationProxy(qt);
        return;
    }
    // Pac: Chromium flag above is the path QtWebEngine documents; clear the
    // QNetworkProxy so a stale manual host does not win after the PAC goes away.
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
}

void setApply(Apply apply)
{
    applySlot() = apply ? std::move(apply) : Apply(applyWithQt);
}

void consider(int wifiProfileId, const std::vector<NmNet::ProxyInfo>& list)
{
    const NmNet::AppProxy next = NmNet::appProxyFor(list, wifiProfileId);
    if (next == g_last)
        return;
    g_last = next;
    applySlot()(next);
}

// For install() and tests: the next consider always runs apply.
void reset()
{
    g_last = {};
}

int wifiProfileIdOf(const QJsonObject& status)
{
    const QJsonObject wifi = status.value(QStringLiteral("wifi")).toObject();
    if (wifi.value(QStringLiteral("state")).toString() != QLatin1String("connected"))
        return 0;
    const QJsonValue id = wifi.value(QStringLiteral("profileId"));
    if (id.isDouble())
        return id.toInt();
    if (id.isString())
        return id.toString().toInt();
    return 0;
}

std::vector<NmNet::ProxyInfo> proxiesOf(const QJsonObject& reply)
{
    std::vector<NmNet::ProxyInfo> out;
    const QJsonArray list = reply.value(QStringLiteral("proxyInfoList")).toArray();
    for (const QJsonValue& value : list) {
        const QJsonObject object = value.toObject();
        NmNet::ProxyInfo info;
        info.networkTechnology = object.value(QStringLiteral("networkTechnology")).toString().toStdString();
        info.proxyScope = object.value(QStringLiteral("proxyScope")).toString().toStdString();
        // Scope may arrive as a number from older callers.
        if (info.proxyScope.empty() && object.contains(QStringLiteral("proxyScope")))
            info.proxyScope = QString::number(object.value(QStringLiteral("proxyScope")).toInt()).toStdString();
        info.proxyConfigType = object.value(QStringLiteral("proxyConfigType")).toString().toStdString();
        info.proxyServer = object.value(QStringLiteral("proxyServer")).toString().toStdString();
        info.proxyAutoConfigUrl =
            object.value(QStringLiteral("proxyAutoConfigUrl")).toString().toStdString();
        if (object.contains(QStringLiteral("proxyPort"))) {
            info.proxyPort = object.value(QStringLiteral("proxyPort")).toInt();
            info.hasPort = true;
        }
        if (object.contains(QStringLiteral("isProxySecured"))) {
            info.isProxySecured = object.value(QStringLiteral("isProxySecured")).toBool();
            info.hasSecured = true;
        }
        if (info.networkTechnology.empty() || info.proxyScope.empty() || info.proxyConfigType.empty())
            continue;
        out.push_back(info);
    }
    return out;
}

} // namespace NetworkAppProxy
