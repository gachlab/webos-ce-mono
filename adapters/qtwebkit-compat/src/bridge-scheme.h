// The webos-bridge:/// scheme and the objects published across it.
//
// Defined in bridge-scheme.cpp. Frame publishing and page construction need
// the shared profile and the pending-window table; they include this header
// and not the script bodies. Downloads are handed to a hook the page
// registers, so this concern does not include the public QWebPage API.

#ifndef QTWEBKIT_COMPAT_BRIDGE_SCHEME_H
#define QTWEBKIT_COMPAT_BRIDGE_SCHEME_H

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QUrl>

class QMetaObject;
class QObject;
class QWebEnginePage;
class QWebEngineProfile;

namespace qtwebkit_compat {
namespace bridge {

QHash<int, QString>& pendingWindowFeatures();
QWebEngineProfile* sharedProfile();
int publishObject(QObject* object, QWebEnginePage* page);
QJsonObject describe(const QMetaObject* meta);

// Who turns an engine download into QWebPage::downloadRequested. The page
// concern registers this; until it does, downloads are still cancelled.
using DownloadHook = void (*)(const QUrl& url, const QString& mimeType, QObject* enginePageParent);
void setDownloadHook(DownloadHook hook);

} // namespace bridge
} // namespace qtwebkit_compat

#endif // QTWEBKIT_COMPAT_BRIDGE_SCHEME_H
