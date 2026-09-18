// Injected document scripts: names and bodies.
//
// Defined in scripts.cpp. Page construction installs them; prepareNewDocument
// rewrites the bridge core. Other translation units include this header for
// those constants only — not the bridge, not the page.

#ifndef QTWEBKIT_COMPAT_SCRIPTS_H
#define QTWEBKIT_COMPAT_SCRIPTS_H

namespace qtwebkit_compat {
namespace scripts {

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

} // namespace scripts
} // namespace qtwebkit_compat

#endif // QTWEBKIT_COMPAT_SCRIPTS_H
