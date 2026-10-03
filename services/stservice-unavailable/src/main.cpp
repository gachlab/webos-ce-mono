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

//
// com.palm.stservice -- Touch to Share -- answered as "unavailable".
//
// On the TouchPad this carried the browser's current page over Bluetooth
// (tap2share) to a paired Pre 3. This machine has no tap sensor and no paired
// phone, and ticket #43 leaves the backend undecided. What it does not leave
// undecided is that the callers must not hang or log a missing service:
//
//   isis-browser's BrowserApp.js          shareData, from the Share menu
//   luna-systemui's SystemManagerAlerts.js acceptShareRequest, rejectShareRequest,
//                                          tryAgain, cancel, bluetoothIsOffResponse
//
// all fire-and-forget through enyo.PalmService. So this service exists only to
// answer them cleanly -- a failing shareData (the page was not shared, with a
// reason) and a succeeding acknowledgement for the dialog methods. The payloads
// are in share_replies.h, free of the bus so they can be pinned by a test; this
// file is just the registration and the method table.
//
// It is deliberately tiny and has no D-Bus client: there is no backend to talk
// to. When one is chosen (#43 sketches a paired phone), it replaces
// share_replies.h; the shape of the service -- one name, these six methods --
// is already what its callers expect.
//

#include "share_replies.h"

#include <luna-service2/lunaservice.h>

#include <glib.h>
#include <glib-unix.h>

#include <string>

namespace {

const char kServiceName[] = "com.palm.stservice";
const char kCategory[] = "/";

GMainLoop* g_loop = nullptr;
LSPalmService* g_service = nullptr;

void logAndFree(const char* where, LSError& error)
{
    g_warning("stservice-unavailable: %s: %s", where, error.message ? error.message : "(no message)");
    LSErrorFree(&error);
}

void reply(LSHandle* sh, LSMessage* message, const std::string& payload)
{
    LSError error;
    LSErrorInit(&error);
    if (!LSMessageReply(sh, message, payload.c_str(), &error))
        logAndFree("LSMessageReply", error);
}

// The browser's Share menu. Answered as a clean failure: the page was not
// shared, and the reason is in the payload.
bool shareData(LSHandle* sh, LSMessage* message, void*)
{
    reply(sh, message, ShareReplies::unavailablePayload());
    return true;
}

// The system UI's dialog buttons. Acknowledged so the UI closes; there is
// nothing pending to act on, but the dismissal is not an error.
bool acknowledge(LSHandle* sh, LSMessage* message, void*)
{
    reply(sh, message, ShareReplies::acknowledgedPayload());
    return true;
}

LSMethod kMethods[] = {
    { "shareData", shareData },
    { "acceptShareRequest", acknowledge },
    { "rejectShareRequest", acknowledge },
    { "tryAgain", acknowledge },
    { "cancel", acknowledge },
    { "bluetoothIsOffResponse", acknowledge },
    { },
};

gboolean quit(gpointer)
{
    g_main_loop_quit(g_loop);
    return G_SOURCE_REMOVE;
}

} // namespace

int main()
{
    g_loop = g_main_loop_new(nullptr, FALSE);

    LSError error;
    LSErrorInit(&error);
    if (!LSRegisterPalmService(kServiceName, &g_service, &error)) {
        logAndFree("LSRegisterPalmService", error);
        return 1;
    }

    // Both buses: the browser is an app (public) and the system UI is the shell
    // (private), and HP's own services registered a role on each.
    LSErrorInit(&error);
    if (!LSPalmServiceRegisterCategory(g_service, kCategory, kMethods, kMethods,
                                       nullptr, nullptr, &error)) {
        logAndFree("LSPalmServiceRegisterCategory", error);
        return 1;
    }

    LSErrorInit(&error);
    if (!LSGmainAttachPalmService(g_service, g_loop, &error)) {
        logAndFree("LSGmainAttachPalmService", error);
        return 1;
    }

    g_unix_signal_add(SIGTERM, quit, nullptr);
    g_unix_signal_add(SIGINT, quit, nullptr);

    g_message("stservice-unavailable: com.palm.stservice up (answering unavailable)");
    g_main_loop_run(g_loop);

    LSErrorInit(&error);
    if (!LSUnregisterPalmService(g_service, &error))
        logAndFree("LSUnregisterPalmService", error);
    g_main_loop_unref(g_loop);
    return 0;
}
