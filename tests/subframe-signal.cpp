// A signal answers only the main frame.
//
// The bridge's return path ends in SignalRelay::deliver(), which calls
// QWebEnginePage::runJavaScript -- and that runs in the main frame alone. The
// bridge core keeps its proxies and their handlers in a per-frame map inside
// its own closure, so an object a child frame proxied registers its handlers
// there, in that frame. The reply is then emitted into the main frame's map,
// which has never heard of that id, and window.__webosBridge.emit returns
// without calling anything.
//
// Measured in the running shell, on the mail card, with a recorder wrapped
// around the main frame's emit: firing one service call from the account
// wizard's iframe logged
//
//   emitLog: [{id: 98, name: "response"}, {id: 87, name: "response"}]
//
// in the MAIN frame, while the iframe's own callback never ran and its probe
// stayed "pending". The same call made from the main frame came back with the
// template list, so the service and the outbound half are both fine.
//
// That is what left AccountWizard.protValidators unassigned -- it is only ever
// set from the listAccountTemplates callback -- and threw
//
//   Uncaught TypeError: Cannot read properties of undefined (reading 'GOOGLE')
//     at AccountWizard._validateProtocolSettings (AccountWizard.js:1047)
//
// when the user pressed the button to add an account.
//
// Ids are handed out by one counter for the whole page, so they are unique
// across frames and emitting into every frame reaches exactly the owner.

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
    std::printf("%-56s %-12s %s\n", what, qPrintable(got),
                ok ? "ok" : qPrintable("FAILED, wanted " + expected));
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QWebPage page;

    // Stands in for PalmServiceBridgeAdapter: an object with a signal,
    // published the way addPalmSystemObject() publishes its own.
    QTimer* probe = new QTimer(&page);

    bool loaded = false;
    QObject::connect(&page, &QWebPage::loadFinished, &page, [&](bool) { loaded = true; });
    QObject::connect(page.mainFrame(), &QWebFrame::javaScriptWindowObjectCleared,
                     page.mainFrame(), [&]() {
        page.mainFrame()->addToJavaScriptWindowObject("Probe", probe);
    });

    page.mainFrame()->setHtml(QStringLiteral("<html><body></body></html>"));

    if (!waitFor([&]() { return loaded; }, 15000)) {
        std::printf("the page never finished loading\n");
        return 1;
    }

    const auto js = [&](const QString& code) {
        return page.mainFrame()->evaluateJavaScript(code).toString();
    };

    // Both frames listen to the same signal of the same object, the way the
    // wizard's iframe and the mail card's main frame both hold bridges.
    js(QStringLiteral(
        "window.__mainFired = 0;"
        "window.Probe.timeout.connect(function () { window.__mainFired++; });"
        "window.__f = document.createElement('iframe');"
        "document.body.appendChild(window.__f);"
        "window.__w = window.__f.contentWindow;"));

    // The frame must have got a bridge at all, or this proves nothing.
    check("the frame has its own proxy of the object",
          js("String(typeof window.__w.Probe)"), "object");

    // Connecting goes in an evaluation of its own. The document a frame starts
    // with is replaced as it is appended, and the bridge's script has to have
    // run in the new one before anything there can be connected to -- doing it
    // in the same breath as the appendChild above reaches the document on its
    // way out and throws on an undefined Probe.
    check("the frame connects a handler of its own",
          js("window.__w.__childFired = 0;"
             "window.__w.Probe.timeout.connect(function () { window.__w.__childFired++; });"
             "String(window.__w.Probe.timeout.__handlers.length)"), "1");

    probe->start(30);

    const auto mainFired = [&]() { return js("String(window.__mainFired > 0)") == "true"; };
    const auto childFired = [&]() { return js("String(window.__w.__childFired > 0)") == "true"; };

    if (!waitFor(mainFired, 15000))
        std::printf("note: the main frame's handler never ran either\n");
    // The main frame has already been answered by now, so the child has had
    // every chance the main frame had.
    waitFor(childFired, 5000);

    probe->stop();

    check("the main frame's handler runs",
          js("String(window.__mainFired > 0)"), "true");
    check("and so does the handler the frame connected",
          js("String(window.__w.__childFired > 0)"), "true");

    return failures == 0 ? 0 : 1;
}
