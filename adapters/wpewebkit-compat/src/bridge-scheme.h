// The webos-bridge:/// scheme and published QObject proxies (WPE port of
// qtwebkit-compat's bridge-scheme).

#ifndef WPEWEBKIT_COMPAT_BRIDGE_SCHEME_H
#define WPEWEBKIT_COMPAT_BRIDGE_SCHEME_H

#include <QHash>
#include <QJsonObject>
#include <QString>

class QMetaObject;
class QObject;
class QWebPage;

namespace wpewebkit_compat {
namespace bridge {

QHash<int, QString>& pendingWindowFeatures();
int publishObject(QObject* object, QWebPage* page);
QJsonObject describe(const QMetaObject* meta);
void ensureRegistered();
void runJavaScript(QWebPage* page, const QString& script);

} // namespace bridge
} // namespace wpewebkit_compat

#endif
