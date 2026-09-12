// enyo cancels animation frames with clearTimeout, and kills other people's
// timers.
//
// dom/util.js sets the pair up like this:
//
//     var builtin = window.webkitRequestAnimationFrame;
//     enyo.requestAnimationFrame = builtin ? enyo.bind(window, builtin) : ...
//     var builtin = window.webkitCancelRequestAnimationFrame || window.clearTimeout;
//     enyo.cancelRequestAnimationFrame = enyo.bind(window, builtin);
//
// In this engine webkitRequestAnimationFrame still exists but
// webkitCancelRequestAnimationFrame does not, so that || quietly settles on
// clearTimeout: enyo asks for frames from the real scheduler and cancels them
// from the timer one. The two number their handles separately, so every cancel
// clears whichever timeout happens to hold that number.
//
// Measured in the running shell: enyo.cancelRequestAnimationFrame(55) killed a
// plain setTimeout whose id was 55, and the scroller does that 12245 times in
// 14 seconds as it starts and stops. That is what stops the mail card's fade --
// enyo.transitions.Fade keeps its chain in a single handle, the scroller cancels
// a frame numbered the same, and the animation never ticks again: the outgoing
// view stays half faded and Pane._transitioning is never set back to false, so
// the card shows two views at once and queues every later view change.
//
// The compat layer gives the prefixed canceller back, so HP's || finds it and
// cancels frames instead of timers.

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

#include <QWebFrame>
#include <QWebPage>

#include <cstdio>
#include <functional>

static bool waitFor(const std::function<bool()>& done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

static int failures = 0;

static void check(const char* what, const QString& got, const QString& expected)
{
    const bool ok = got == expected;
    if (!ok)
        ++failures;
    std::printf("%-56s %-10s %s\n", what, qPrintable(got),
                ok ? "ok" : qPrintable("FAILED, wanted " + expected));
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QWebPage page;
    bool loaded = false;
    QObject::connect(&page, &QWebPage::loadFinished, &page, [&](bool) { loaded = true; });

    // HP's two lines, run verbatim, and then the collision they cause: cancel
    // using the id of a plain timeout. With the prefixed canceller present that
    // is an unknown frame handle and the timeout survives; without it the call
    // is clearTimeout and the timeout dies.
    page.mainFrame()->setHtml(QStringLiteral(
        "<html><body><script>"
        "  window.__hasPrefixedCancel = (typeof window.webkitCancelRequestAnimationFrame);"
        "  var request = window.webkitRequestAnimationFrame;"
        "  var builtin = window.webkitCancelRequestAnimationFrame || window.clearTimeout;"
        "  window.__cancelIsClearTimeout = (builtin === window.clearTimeout);"
        "  var cancel = function () { return builtin.apply(window, arguments); };"
        "  window.__timerFired = false;"
        "  var timerId = window.setTimeout(function () { window.__timerFired = true; }, 30);"
        "  cancel(timerId);"
        "  window.__frameRan = false;"
        "  if (request) {"
        "    var frameId = request.call(window, function () { window.__frameRan = true; });"
        "    cancel(frameId);"
        "  }"
        "  window.__done = false;"
        "  window.setTimeout(function () { window.__done = true; }, 200);"
        "</script></body></html>"));

    if (!waitFor([&]() { return loaded; }, 15000)) {
        std::printf("the page never finished loading\n");
        return 1;
    }

    const auto js = [&](const QString& code) {
        return page.mainFrame()->evaluateJavaScript(code).toString();
    };

    if (!waitFor([&]() { return js("String(window.__done)") == "true"; }, 10000))
        std::printf("note: the page's own timer never finished\n");

    // What enyo looks for.
    check("the prefixed canceller is there to be found",
          js("String(window.__hasPrefixedCancel)"), "function");
    // ...so HP's || must not fall through to the timer API.
    check("enyo's || does not settle on clearTimeout",
          js("String(window.__cancelIsClearTimeout)"), "false");
    // The damage that fallthrough does: an unrelated timer with the same number.
    check("cancelling a frame leaves a plain timer alone",
          js("String(window.__timerFired)"), "true");
    // And it still has to do its own job.
    check("cancelling a frame does cancel the frame",
          js("String(window.__frameRan)"), "false");

    return failures == 0 ? 0 : 1;
}
