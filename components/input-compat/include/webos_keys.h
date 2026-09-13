/* @@@LICENSE
 *
 * Copyright (c) 2026 webOS CE modern build
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * LICENSE@@@ */

#ifndef WEBOS_KEYS_H
#define WEBOS_KEYS_H

#include <Qt>

//
// webOS key codes that have to become Qt key codes before they reach a web
// engine.
//
// SysMgrDeviceKeydefs.h has its own `enum Key`, built when a key code was a
// character: Key_Escape = 0x1B, and Key_CoreNavi_Back is an alias of it. Qt's
// catalogue is a different one -- Qt::Key_Escape is 0x01000000 -- and the two
// were never reconciled, because under QtWebKit the key never travelled as a
// Qt key at all: WindowedWebApp::keyEvent ran every code through
// WebKitKeyMap::translateKey and handed WebCore a number of its own. That
// whole branch is commented out in this port; what runs now is
// `bridge->page()->event(e)`, so the QKeyEvent reaches Chromium as-is.
//
// MEASURED in the running browser, with a listener on the app's document and
// 13 back gestures from the strip:
//
//     KEYPROBE keydown keyCode=0 which=0 key="Escape" target=INPUT#input-29
//
// The name arrives, the number does not. Chromium derives the DOM keyCode from
// the native/Windows virtual key, which it looks up from the Qt key code; 27 is
// not one, so the lookup yields 0. enyo's Gesture.js synthesises the "back"
// event from `if (e.keyCode == 27)` alone, so it never fires and the browser
// never goes back. The same escape injected through the DevTools protocol, with
// keyCode 27, does produce it -- measured, as `enyo-back-dispatched`.
//
// So this is a translation and nothing else: no policy, no event invented, no
// behaviour added. It says which Qt key a webOS key code means. The text
// matters as much as the number -- Chromium builds `key` and the character from
// it, and an empty text on a key that has a character is how a keypress goes
// missing.
//
namespace WebosKeys {

// SysMgrDeviceKeydefs.h values. Named here rather than included: this header is
// built into tests that have no luna-sysmgr-ipc-messages on their include path,
// and these three numbers are frozen by an IPC protocol in any case.
enum WebosKey {
    kBack = 0x1B,       // Key_Escape, and Key_CoreNavi_Back aliases it
    kMenu = 0xE0E5,     // Key_CoreNavi_Menu
    kPrevious = 0xE0E2, // Key_CoreNavi_Previous
    kNext = 0xE0E3,     // Key_CoreNavi_Next
};

struct QtKey {
    int key;            // a Qt::Key, or 0 when there is nothing to translate
    const char* text;   // what QKeyEvent's text() must carry, never null

    bool isTranslated() const { return key != 0; }
};

// The webOS key code a card gets over IPC -> the Qt key it has to become.
//
// Only the codes that are wrong are answered. Everything else returns a QtKey
// that says so, and the caller must then leave the original event alone: an
// ordinary letter already arrives as a valid Qt key, and rewriting it would
// break typing to fix a gesture.
inline QtKey toQtKey(int webosKey)
{
    switch (webosKey) {
    case kBack:
        // "\x1b" and not "": Chromium reads the character from the event's
        // text, and Escape has one.
        return QtKey{ Qt::Key_Escape, "\x1b" };
    default:
        // Menu, Previous and Next are deliberately absent. They have no Qt
        // equivalent and no web app listens for them as keys -- webOS delivered
        // them to apps as Mojo gestures, through a path that is commented out
        // in this port. Giving them an arbitrary Qt key here would put
        // keystrokes into pages that never asked for them.
        return QtKey{ 0, "" };
    }
}

} // namespace WebosKeys

#endif /* WEBOS_KEYS_H */
