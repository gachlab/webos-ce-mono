// Shared declarations for the split translation units of qtwebkit-compat.
//
// The layer used to live in one file with an anonymous namespace. Crossing
// translation units needs a named namespace: published objects, the shared
// profile and the injected scripts are one table each, not one per .cpp.

#ifndef QTWEBKIT_COMPAT_DETAIL_H
#define QTWEBKIT_COMPAT_DETAIL_H

#include "qtwebkit_compat.h"

#include <QHash>
#include <QJsonObject>
#include <QMetaObject>
#include <QString>

class QObject;
class QWebEnginePage;
class QWebEngineProfile;

namespace qtwebkit_compat_detail {

extern const char kScheme[];

extern const char kInjectedScriptName[];
extern const char kBorderImageScriptName[];
extern const char kPrefixedEventScriptName[];
extern const char kAppViewShimScriptName[];
extern const char kFrameCancelScriptName[];
extern const char kFlexWidthScriptName[];
extern const char kEnyoWheelScriptName[];
extern const char kNumberInputScriptName[];
extern const char kWindowOpenScriptName[];
extern const char kRemoteRequestScriptName[];
extern const char kBrowserViewScriptName[];

extern const char kAppViewShims[];
extern const char kPrefixedEvents[];
extern const char kFrameCancel[];
extern const char kBorderImageCompat[];
extern const char kFlexWidthCompat[];
extern const char kEnyoWheelCompat[];
extern const char kNumberInputs[];
extern const char kBridgeCore[];
extern const char kBrowserView[];
extern const char kWindowOpen[];
extern const char kRemoteRequests[];

QHash<int, QString>& pendingWindowFeatures();
QWebEngineProfile* sharedProfile();
int publishObject(QObject* object, QWebEnginePage* page);
QJsonObject describe(const QMetaObject* meta);

} // namespace qtwebkit_compat_detail

#endif // QTWEBKIT_COMPAT_DETAIL_H
