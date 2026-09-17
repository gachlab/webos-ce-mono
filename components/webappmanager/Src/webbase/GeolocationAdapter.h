// Web pages' location, through com.palm.location.
//
// On a device WebKit asked com.palm.location itself (GeolocationServicePalm,
// and the browser's GeolocationAdapter plugin), and the service put the system
// UI's "Location Services" alert up for each website. QtWebEngine asks two
// other parties instead: its embedder, whether a site may have the location,
// and Qt Positioning, where it is. This answers both from com.palm.location:
//
//   * the permission, through QWebPage's geolocation policy: a
//     getCurrentPosition carrying the site's url, as WebKit's did, so the
//     service asks the user and remembers the answer;
//   * the positions, through a Qt Positioning source registered as a static
//     plugin of WebAppMgr, which the engine creates as its default one.
//
// ADAPTER: none of HP's code is involved; WebAppManager only installs it.

#ifndef GEOLOCATIONADAPTER_H
#define GEOLOCATIONADAPTER_H

#include <functional>

#include <QGeoPositionInfoSource>
#include <QJsonObject>
#include <QString>

#include <QWebPage>

struct LSHandle;

namespace Geolocation {

// A reply from com.palm.location: once for a plain call, each time for a
// subscription.
using Reply = std::function<void(const QJsonObject& reply)>;
// Calls a com.palm.location method; what it returns cancels the call.
using Call = std::function<std::function<void()>(const QString& method, const QJsonObject& payload, Reply reply)>;

// The errors that mean "no": location services off, terms not accepted, and
// the user refusing. Anything else -- no position yet -- still lets the site
// ask, and it is told the error then.
bool refuses(const QJsonObject& reply);

QWebPage::GeolocationPolicy policy(Call call);

// A position source on com.palm.location.
QGeoPositionInfoSource* createSource(Call call, QObject* parent);

// Makes pages and the engine use com.palm.location through `call`.
void setDefault(Call call);

// com.palm.location on the bus, through `handle`.
Call busCall(LSHandle* handle);

// setDefault(busCall(handle)).
void install(LSHandle* handle);

} // namespace Geolocation

#endif
