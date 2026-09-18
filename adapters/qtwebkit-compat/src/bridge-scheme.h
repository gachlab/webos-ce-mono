// The webos-bridge:/// scheme and the objects published across it.
//
// Defined in bridge-scheme.cpp. Frame publishing and page construction need
// the shared profile and the pending-window table; they include this header
// and not the script bodies.

#ifndef QTWEBKIT_COMPAT_BRIDGE_SCHEME_H
#define QTWEBKIT_COMPAT_BRIDGE_SCHEME_H

#include <QHash>
#include <QJsonObject>
#include <QMetaObject>
#include <QString>

class QObject;
class QWebEnginePage;
class QWebEngineProfile;

namespace qtwebkit_compat {
namespace bridge {

extern const char kScheme[];

QHash<int, QString>& pendingWindowFeatures();
QWebEngineProfile* sharedProfile();
int publishObject(QObject* object, QWebEnginePage* page);
QJsonObject describe(const QMetaObject* meta);

} // namespace bridge
} // namespace qtwebkit_compat

#endif // QTWEBKIT_COMPAT_BRIDGE_SCHEME_H
