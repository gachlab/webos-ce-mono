#include "GeolocationAdapter.h"

#include <memory>

#include <QGeoAreaMonitorSource>
#include <QGeoPositionInfoSourceFactory>
#include <QGeoSatelliteInfoSource>
#include <QPointer>
#include <QTimeZone>
#include <QtPlugin>

namespace Geolocation {

namespace {

// com.palm.location's errorCodes.
const int kServiceOff = 5;
const int kTermsNotAccepted = 6;
const int kDenied = 8;
const int kUnknown = -1;

int errorCodeOf(const QJsonObject& reply)
{
    if (reply.value("returnValue").toBool(true) && reply.value("errorCode").toInt(0) == 0)
        return 0;
    // A hub error (no such service) has no errorCode of the service's.
    return reply.value("errorCode").toInt(kUnknown);
}

Call& defaultCall()
{
    static Call call;
    return call;
}

// HP's levels, 1 the finest: WebKit sent 1 for enableHighAccuracy.
int accuracyFor(QGeoPositionInfoSource::PositioningMethods methods)
{
    return methods == QGeoPositionInfoSource::SatellitePositioningMethods ? 1 : 2;
}

int responseTimeFor(int timeoutMs)
{
    if (timeoutMs <= 0)
        return 2;
    return timeoutMs <= 10000 ? 1 : timeoutMs <= 30000 ? 2 : 3;
}

QGeoPositionInfo positionOf(const QJsonObject& reply)
{
    const double altitude = reply.value("altitude").toDouble(-1);
    QGeoCoordinate coordinate(reply.value("latitude").toDouble(), reply.value("longitude").toDouble());
    if (reply.value("vertAccuracy").toDouble(-1) >= 0)
        coordinate.setAltitude(altitude);
    const QDateTime when = QDateTime::fromMSecsSinceEpoch(qint64(reply.value("timestamp").toDouble()), QTimeZone::UTC);
    QGeoPositionInfo info(coordinate, when.isValid() ? when : QDateTime::currentDateTimeUtc());
    const auto set = [&info, &reply](const char* key, QGeoPositionInfo::Attribute attribute) {
        const double value = reply.value(key).toDouble(-1);
        if (value >= 0)
            info.setAttribute(attribute, value);
    };
    set("horizAccuracy", QGeoPositionInfo::HorizontalAccuracy);
    set("vertAccuracy", QGeoPositionInfo::VerticalAccuracy);
    set("heading", QGeoPositionInfo::Direction);
    set("velocity", QGeoPositionInfo::GroundSpeed);
    return info;
}

// What the engine is told when there is no position.
QGeoPositionInfoSource::Error sourceErrorOf(const QJsonObject& reply)
{
    return refuses(reply) ? QGeoPositionInfoSource::AccessError : QGeoPositionInfoSource::UpdateTimeoutError;
}

class Source : public QGeoPositionInfoSource
{
    Q_OBJECT
public:
    Source(Call call, QObject* parent)
        : QGeoPositionInfoSource(parent)
        , m_call(std::move(call))
    {
    }

    ~Source() override
    {
        cancel(m_tracking);
        cancel(m_single);
    }

    QGeoPositionInfo lastKnownPosition(bool fromSatellitePositioningMethodsOnly) const override
    {
        return fromSatellitePositioningMethodsOnly ? QGeoPositionInfo() : m_last;
    }
    PositioningMethods supportedPositioningMethods() const override { return AllPositioningMethods; }
    int minimumUpdateInterval() const override { return 1000; }
    Error error() const override { return m_error; }

public Q_SLOTS:
    void startUpdates() override
    {
        if (m_tracking)
            return;
        m_error = NoError;
        QPointer<Source> self(this);
        QJsonObject payload{{"subscribe", true}, {"accuracy", accuracyFor(preferredPositioningMethods())}};
        m_tracking = m_call(QStringLiteral("startTracking"), payload, [self](const QJsonObject& reply) {
            if (!self)
                return;
            if (errorCodeOf(reply) != 0) {
                // The subscription is over; the engine may start another.
                cancel(self->m_tracking);
                self->fail(sourceErrorOf(reply));
                return;
            }
            self->deliver(reply);
        });
    }

    void stopUpdates() override { cancel(m_tracking); }

    void requestUpdate(int timeout) override
    {
        if (m_single)
            return;
        m_error = NoError;
        QPointer<Source> self(this);
        QJsonObject payload{{"accuracy", accuracyFor(preferredPositioningMethods())},
                            {"responseTime", responseTimeFor(timeout)}};
        m_single = m_call(QStringLiteral("getCurrentPosition"), payload, [self](const QJsonObject& reply) {
            if (!self)
                return;
            self->m_single = nullptr;
            if (errorCodeOf(reply) != 0) {
                self->fail(refuses(reply) ? AccessError : UpdateTimeoutError);
                return;
            }
            self->deliver(reply);
        });
    }

private:
    static void cancel(std::function<void()>& call)
    {
        if (!call)
            return;
        const std::function<void()> stop = std::move(call);
        call = nullptr;
        stop();
    }

    void deliver(const QJsonObject& reply)
    {
        m_last = positionOf(reply);
        Q_EMIT positionUpdated(m_last);
    }

    void fail(Error error)
    {
        m_error = error;
        Q_EMIT errorOccurred(error);
    }

    Call m_call;
    std::function<void()> m_tracking;
    std::function<void()> m_single;
    QGeoPositionInfo m_last;
    Error m_error = NoError;
};

} // namespace

bool refuses(const QJsonObject& reply)
{
    const int code = errorCodeOf(reply);
    return code == kServiceOff || code == kTermsNotAccepted || code == kDenied || code == kUnknown;
}

QWebPage::GeolocationPolicy policy(Call call)
{
    return [call](const QUrl& origin, std::function<void(bool)> decide) {
        // The position itself is not used: this is how WebKit asked, and what
        // makes the service ask the user about the site. A position that
        // cannot be had yet is not a refusal.
        const QJsonObject payload{{"url", origin.toString()}, {"accuracy", 3}, {"responseTime", 1},
                                  {"maximumAge", 86400}};
        auto decided = std::make_shared<bool>(false);
        call(QStringLiteral("getCurrentPosition"), payload, [decide, decided](const QJsonObject& reply) {
            if (*decided)
                return;
            *decided = true;
            decide(!refuses(reply));
        });
    };
}

QGeoPositionInfoSource* createSource(Call call, QObject* parent)
{
    return new Source(std::move(call), parent);
}

void setDefault(Call call)
{
    defaultCall() = call;
    QWebPage::setGeolocationPolicy(policy(std::move(call)));
}

} // namespace Geolocation

// The engine asks Qt Positioning for its default source, which is the plugin
// with the highest priority. This one is built into WebAppMgr and outranks the
// host's (GeoClue, NMEA), which would skip com.palm.location and its consent.
class WebOSGeoPositionFactory : public QObject, public QGeoPositionInfoSourceFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.qt.position.sourcefactory/6.0" FILE "GeolocationAdapter.json")
    Q_INTERFACES(QGeoPositionInfoSourceFactory)
public:
    QGeoPositionInfoSource* positionInfoSource(QObject* parent, const QVariantMap&) override
    {
        const Geolocation::Call& call = Geolocation::defaultCall();
        return call ? Geolocation::createSource(call, parent) : nullptr;
    }
    QGeoSatelliteInfoSource* satelliteInfoSource(QObject*, const QVariantMap&) override { return nullptr; }
    QGeoAreaMonitorSource* areaMonitor(QObject*, const QVariantMap&) override { return nullptr; }
};

Q_IMPORT_PLUGIN(WebOSGeoPositionFactory)

#include "GeolocationAdapter.moc"
