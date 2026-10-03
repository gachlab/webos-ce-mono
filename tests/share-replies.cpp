// What com.palm.stservice answers, checked without the bus.
//
// Touch to Share has no backend on this machine (no tap sensor, no paired
// phone; ticket #43 leaves it undecided). What is not undecided is that the
// callers must not hang: the browser's Share menu calls shareData and the
// system UI calls the responder methods, all fire-and-forget. So the two reply
// shapes are the whole contract -- a clean failure for shareData, a succeeding
// acknowledgement for the dialog methods -- and this pins them.
#include "share_replies.h"

#include <cstdio>
#include <string>

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("  %-66s %s\n", what, ok ? "OK" : "<-- FAIL");
    if (!ok)
        ++g_failures;
}

static bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

int main()
{
    std::printf("shareData fails cleanly: the page was not shared, and why\n");
    {
        const std::string payload = ShareReplies::unavailablePayload();
        // returnValue:false is what tells the browser the page was not shared.
        check(contains(payload, "\"returnValue\":false"), "it is a failure, not a success");
        check(contains(payload, "\"errorText\":\""), "a reason a log reader understands");
        check(contains(payload, "Touch to Share is not available"), "the reason is unavailability");
        check(contains(payload, "\"errorCode\":-1"), "a stable code a caller could branch on");
    }

    std::printf("the dialog methods are acknowledged so the UI closes\n");
    {
        const std::string payload = ShareReplies::acknowledgedPayload();
        // The user dismissing a dialog is not an error; nothing was pending,
        // but the call succeeded so the UI does not treat its own cancel as a
        // failure.
        check(contains(payload, "\"returnValue\":true"), "acknowledged as handled");
        check(!contains(payload, "\"errorText\""), "no error on a plain acknowledgement");
    }

    std::printf("the two shapes do not agree -- that is the point\n");
    {
        // A mutation that made both the same (e.g. acknowledge every method)
        // would let shareData report success and the browser believe the page
        // was shared when it was not.
        check(ShareReplies::unavailablePayload() != ShareReplies::acknowledgedPayload(),
              "shareData and an acknowledgement are different replies");
    }

    std::printf("\n%s\n", g_failures ? "FAILED" : "all good");
    return g_failures ? 1 : 0;
}
