// A window opened by window.open() gets the bridge too late for its own first
// script.
//
// WebAppMgr publishes its JavaScript side from a javaScriptWindowObjectCleared
// handler: addPalmSystemObject() adds PalmSystem and PalmServiceBridgeFactory
// and evaluates the shim that defines PalmServiceBridge
// (SysMgrWebBridge.cpp:301-322). The compat layer turns that into a script that
// runs at DocumentCreation, before the page's own -- but only while it is
// collecting, and collecting is switched on by prepareNewDocument(), which runs
// from acceptNavigationRequest().
//
// A child window's first document does not arrive that way, so the handler's
// work falls through to the "run it now" branch and lands on a document that
// has already executed its scripts. The mail card is such a window: the email
// launcher opens mail/index.html, and it threw
//
//   Uncaught ReferenceError: PalmServiceBridge is not defined
//     at new EmailApp.Util._ServiceRequest (util.js:307)
//     at AccountWizard._getTemplateList (AccountWizard.js:1607)
//
// while the same page answers "function" for PalmServiceBridge once it is up.
//
// This drives the same shape: a page opens another, the opener publishes an
// object and evaluates a line from the handler, and the child's first script
// says whether it could see either.

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTextStream>
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

// Shaped like SysMgrWebPage::createWindow: build the new page, wire the handler
// that publishes the bridge, and hand it back.
class Opener : public QWebPage
{
public:
    QWebPage* child = nullptr;

    QWebPage* createWindow(WebWindowType) override
    {
        child = new QWebPage(this);
        QObject::connect(child->mainFrame(), &QWebFrame::javaScriptWindowObjectCleared,
                         child->mainFrame(), [this]() {
            // What addPalmSystemObject() does: one published object, one
            // evaluated script that defines a constructor from it.
            child->mainFrame()->addToJavaScriptWindowObject("Probe", new QTimer(child));
            child->mainFrame()->evaluateJavaScript(
                QStringLiteral("window.ProbeBridge = function () { return Probe; };"));
        });
        return child;
    }
};

static bool writeFile(const QString& path, const QString& text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream(&file) << text;
    return true;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("no temporary directory\n");
        return 1;
    }

    // The child records, in its very first script, what the bridge left it --
    // which is the moment the mail card's code runs and throws.
    if (!writeFile(dir.filePath("child.html"), QStringLiteral(
            "<html><body><script>\n"
            "  window.__sawObject = typeof window.Probe;\n"
            "  window.__sawShim = typeof window.ProbeBridge;\n"
            "</script></body></html>\n"))) {
        std::printf("could not write the child page\n");
        return 1;
    }
    if (!writeFile(dir.filePath("opener.html"), QStringLiteral(
            "<html><body><script>window.open('child.html');</script></body></html>\n"))) {
        std::printf("could not write the opener page\n");
        return 1;
    }

    Opener opener;
    opener.mainFrame()->setUrl(QUrl::fromLocalFile(dir.filePath("opener.html")));

    if (!waitFor([&]() { return opener.child != nullptr; }, 15000)) {
        std::printf("the opener never opened a window\n");
        return 1;
    }

    const auto childSays = [&](const QString& expression) {
        return opener.child->mainFrame()->evaluateJavaScript(expression).toString();
    };

    if (!waitFor([&]() { return childSays("String(window.__sawObject)") != "undefined"
                             && childSays("String(window.__sawObject)") != ""; }, 15000))
        std::printf("note: the child's first script never reported\n");

    // Both must be in place before the child's own code runs.
    check("the child's first script sees the published object",
          childSays("String(window.__sawObject)"), "object");
    check("and the constructor the handler evaluated",
          childSays("String(window.__sawShim)"), "function");

    return failures == 0 ? 0 : 1;
}
