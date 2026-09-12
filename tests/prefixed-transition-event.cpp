// enyo ends its transitions on webkitTransitionEnd, and Chromium stopped
// firing it.
//
// Measured in this engine: a listener on "webkitTransitionEnd" is called zero
// times, one on "transitionend" once. enyo registers only the prefixed name, and
// enyo.Pane keeps a transition in flight until that handler runs -- flow() only
// hides a view that is neither current nor transitioning. So the outgoing view
// stayed visible and the mail card painted two views on top of each other.
//
// The compat layer registers a prefixed listener for the modern name as well.
// This checks that it arrives, that the modern name still works on its own, and
// that removing a prefixed listener really removes it.

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

static void check(const char* what, int got, int expected)
{
    const bool ok = got == expected;
    if (!ok)
        ++failures;
    std::printf("%-52s %d (expected %d)  %s\n", what, got, expected, ok ? "ok" : "FAILED");
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QWebPage page;
    bool loaded = false;
    QObject::connect(&page, &QWebPage::loadFinished, &page, [&](bool) { loaded = true; });

    page.mainFrame()->setHtml(QStringLiteral(
        "<html><body style='margin:0'>"
        "<div id='box' style='width:80px;height:80px;background:#f00;opacity:1;"
        "  -webkit-transition:opacity 0.2s linear; transition:opacity 0.2s linear'></div>"
        "<div id='gone' style='width:80px;height:80px;background:#00f;opacity:1;"
        "  -webkit-transition:opacity 0.2s linear; transition:opacity 0.2s linear'></div>"
        "<script>"
        "  window.__prefixed = 0; window.__modern = 0; window.__removed = 0;"
        "  var box = document.getElementById('box');"
        "  box.addEventListener('webkitTransitionEnd', function () { window.__prefixed++; }, false);"
        "  box.addEventListener('transitionend', function () { window.__modern++; }, false);"
        "  var gone = document.getElementById('gone');"
        "  var doomed = function () { window.__removed++; };"
        "  gone.addEventListener('webkitTransitionEnd', doomed, false);"
        "  gone.removeEventListener('webkitTransitionEnd', doomed, false);"
        "  setTimeout(function () { box.style.opacity = '0'; gone.style.opacity = '0'; }, 50);"
        "</script></body></html>"));

    if (!waitFor([&]() { return loaded; }, 15000)) {
        std::printf("the page never finished loading\n");
        return 1;
    }

    const auto count = [&](const char* name) {
        return page.mainFrame()
            ->evaluateJavaScript(QStringLiteral("window.%1").arg(name))
            .toInt();
    };

    // The transition lasts 200 ms; give it room without making the test slow.
    if (!waitFor([&]() { return count("__modern") > 0; }, 10000))
        std::printf("note: the modern event never arrived either\n");
    QEventLoop settle;
    QTimer::singleShot(300, &settle, &QEventLoop::quit);
    settle.exec();

    // What enyo listens for, and what it is really called by.
    check("a webkitTransitionEnd listener is called", count("__prefixed"), 1);
    check("a transitionend listener still works", count("__modern"), 1);
    // Registering under two names must not break removal.
    check("a removed prefixed listener stays removed", count("__removed"), 0);

    return failures == 0 ? 0 : 1;
}
