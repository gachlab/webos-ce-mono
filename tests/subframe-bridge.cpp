// Nothing of the bridge reaches an iframe.
//
// The compat layer turns what WebAppMgr adds from its
// javaScriptWindowObjectCleared handler into a QWebEngineScript that runs at
// DocumentCreation. A QWebEngineScript runs in the main frame only unless it
// says otherwise: runsOnSubFrames defaults to false. QtWebKit had no such
// switch -- it cleared and repopulated every frame's global object -- so HP's
// code assumes a frame is a frame.
//
// Measured in the running shell, on the mail card: the main frame answers
// "function" for PalmServiceBridge while all three of its iframes answer
// "undefined" for PalmServiceBridge, PalmSystem and even __webosBridge, the
// bridge's own core. The email app loads ../accounts/ into the mail card, so
// AccountWizard runs there and threw
//
//   Uncaught ReferenceError: PalmServiceBridge is not defined
//     at new EmailApp.Util._ServiceRequest (util.js:307)
//     at AccountWizard._getTemplateList (AccountWizard.js:1607)
//
// when the user asked to add an account.
//
// This checks the frame a page makes for itself, with no src -- which is the
// shape the mail card's three have.

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
    bool loaded = false;
    QObject::connect(&page, &QWebPage::loadFinished, &page, [&](bool) { loaded = true; });

    // What addPalmSystemObject() does: publish an object and evaluate a line
    // that builds something from it, both from the handler.
    QObject::connect(page.mainFrame(), &QWebFrame::javaScriptWindowObjectCleared,
                     page.mainFrame(), [&]() {
        page.mainFrame()->addToJavaScriptWindowObject("Probe", new QTimer(&page));
        page.mainFrame()->evaluateJavaScript(
            QStringLiteral("window.ProbeBridge = function () { return Probe; };"));
    });

    page.mainFrame()->setHtml(QStringLiteral("<html><body></body></html>"));

    if (!waitFor([&]() { return loaded; }, 15000)) {
        std::printf("the page never finished loading\n");
        return 1;
    }

    const auto js = [&](const QString& code) {
        return page.mainFrame()->evaluateJavaScript(code).toString();
    };

    // Make the frame the way the app does, and ask it what it has.
    js(QStringLiteral(
        "window.__f = document.createElement('iframe');"
        "document.body.appendChild(window.__f);"
        "window.__w = window.__f.contentWindow;"));

    check("the main frame has the published object",
          js("String(typeof window.Probe)"), "object");
    check("and so does the frame the page just made",
          js("String(typeof window.__w.Probe)"), "object");
    check("the shim the handler evaluated reaches it too",
          js("String(typeof window.__w.ProbeBridge)"), "function");
    check("and the bridge core itself",
          js("String(typeof window.__w.__webosBridge)"), "object");

    return failures == 0 ? 0 : 1;
}
