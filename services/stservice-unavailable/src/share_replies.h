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

#ifndef STSERVICE_SHARE_REPLIES_H
#define STSERVICE_SHARE_REPLIES_H

//
// What com.palm.stservice answers, kept here and free of the bus so
// tests/share-replies.cpp can pin the exact payloads without ls-hubd.
//
// Touch to Share was HP's "tap to share": the TouchPad's browser called
// shareData with the current page, and tap2share carried it over Bluetooth to a
// paired Pre 3; the system UI showed the incoming share and the responder
// methods (acceptShareRequest / rejectShareRequest / tryAgain / cancel /
// bluetoothIsOffResponse) drove its dialogs.
//
// None of that exists on this machine: there is no tap sensor and no paired
// phone, and ticket #43 leaves the backend undecided. What it does NOT leave
// undecided is that the callers must not hang or log a missing service: the
// browser's Share menu calls shareData (BrowserApp.js) and the system UI calls
// the responder methods (SystemManagerAlerts.js), all fire-and-forget through
// enyo.PalmService. So this service exists only to answer, cleanly, that Touch
// to Share is unavailable -- which is a listed "done when" of the ticket.
//
// Two shapes, because the two kinds of call mean different things:
//
//   shareData            an action that cannot be carried out -> a failure the
//                        browser hears as "not shared", with an errorText a log
//                        reader understands and an errorCode a caller can
//                        branch on. HP's shareData could fail (Bluetooth off,
//                        no peer), so a failing reply is within its own
//                        contract, not a new error shape.
//   the responder methods acknowledgements of a dialog the user dismissed.
//                        There is nothing to accept, reject or retry, and the
//                        dialogs are only ever raised by a share that this
//                        machine never starts, but if one is somehow invoked
//                        the honest answer is "handled" -- returnValue:true --
//                        so the UI closes rather than treating its own cancel
//                        as an error.
//
// When a backend is chosen (sharing with a paired phone was the sketch in #43),
// these replies are what it replaces; until then they are the whole service.
//

#include <string>

namespace ShareReplies {

// The one error code for "there is no Touch to Share here". Negative so it
// cannot collide with a real HP status, and constant so a future caller that
// chooses to branch on it has something stable to match.
inline int unavailableCode() { return -1; }

inline const char* unavailableText()
{
    return "Touch to Share is not available on this device";
}

// shareData's reply: a clean failure. returnValue:false is what tells the
// browser the page was not shared; the text and code say why, for a log and for
// any caller that looks.
inline std::string unavailablePayload()
{
    return std::string("{\"returnValue\":false,\"errorCode\":")
         + std::to_string(unavailableCode())
         + ",\"errorText\":\"" + unavailableText() + "\"}";
}

// The responder methods' reply: handled. Nothing happened because nothing was
// pending, but the dialog's own cancel/accept is not an error, so the UI is
// told the call succeeded and closes.
inline std::string acknowledgedPayload()
{
    return "{\"returnValue\":true}";
}

} // namespace ShareReplies

#endif // STSERVICE_SHARE_REPLIES_H
