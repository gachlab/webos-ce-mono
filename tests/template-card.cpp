// The template card, in the engine that runs it.
//
// The library's own tests (components/cards/test) need no browser. This one is
// the other half: the card as WebAppMgr loads it -- the built bundle, the
// custom elements, HP's stylesheet -- against a PalmServiceBridge that answers
// the way com.palm.deviceprofile does, and then the way a service that is not
// running does.
//
// Verified by mutation: without the kit's elements, without the card asking
// the bus, or with the retry not asking again, this turns red.

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>

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
    std::printf("%-56s %-28s %s\n", what, qPrintable(got.left(28)),
                ok ? "ok" : qPrintable("FAILED, wanted " + expected));
}

// WebAppMgr's bridge, as the card finds it on the page: one object per call,
// answers from a table the test fills in.
static const char kFakeBridge[] = R"JS(
window.__calls = [];
window.__answers = {};
window.PalmServiceBridge = function () { this.onservicecallback = null; };
window.PalmServiceBridge.prototype.call = function (uri, payload) {
    var self = this;
    window.__calls.push(uri);
    var answer = window.__answers[uri];
    if (answer === undefined) return;
    setTimeout(function () {
        if (self.onservicecallback) self.onservicecallback(JSON.stringify(answer));
    }, 0);
};
window.PalmServiceBridge.prototype.cancel = function () { this.onservicecallback = null; };
window.PalmSystem = { stageReady: function () { window.__ready = true; } };
window.__answers["palm://com.palm.deviceprofile/getDeviceProfile"] =
    { returnValue: true, deviceInfo: { deviceModel: "ZBook", softwareVersion: "webOS-CE-3.0.5", nduId: "abc" } };
)JS";

// What the card shows, reaching into the elements' shadow roots -- which is
// where a custom element keeps what it drew.
static const char kRowTitles[] = R"JS(
Array.prototype.slice.call(document.querySelectorAll("hp-row"))
    .map(function (row) {
        var t = row.shadowRoot && row.shadowRoot.querySelector(".hp-row-title");
        return t ? t.textContent : "";
    }).join("|")
)JS";

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const QString built = QString::fromLocal8Bit(qgetenv("WEBOS_CARDS_BUILD"));
    if (built.isEmpty() || !QFile::exists(built + "/com.palm.app.template/main.js")) {
        std::printf("SKIP: the cards are not built (tools/build-cards.sh)\n");
        return 77;
    }

    // The card's own page, with the bridge put there before its bundle runs,
    // which is how WebAppMgr has it.
    QTemporaryDir dir;
    const QString source = built + "/com.palm.app.template";
    for (const QString& name : { QStringLiteral("main.js"), QStringLiteral("hp.css") })
        QFile::copy(source + "/" + name, dir.filePath(name));
    QFile page(dir.filePath("index.html"));
    if (!page.open(QIODevice::WriteOnly))
        return 1;
    page.write("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
               "<link rel=\"stylesheet\" href=\"hp.css\"></head><body><div id=\"card\"></div>"
               "<script>");
    page.write(kFakeBridge);
    page.write("</script><script src=\"main.js\"></script></body></html>");
    page.close();

    QWebPage card;
    bool loaded = false;
    QObject::connect(&card, &QWebPage::loadFinished, &card, [&loaded]() { loaded = true; });
    card.mainFrame()->load(QUrl::fromLocalFile(dir.filePath("index.html")));
    if (!waitFor([&]() { return loaded; }, 20000)) {
        std::printf("the card never loaded\n");
        return 1;
    }
    const auto js = [&](const char* code) { return card.mainFrame()->evaluateJavaScript(code).toString(); };

    // It asks the bus, and tells WebAppMgr it is ready to be shown.
    waitFor([&]() { return js("String(window.__calls.length)") != "0"; }, 5000);
    check("the card asks com.palm.deviceprofile",
          js("String(window.__calls.indexOf('palm://com.palm.deviceprofile/getDeviceProfile') >= 0)"),
          QStringLiteral("true"));
    check("and watches the connection while it is on screen",
          js("String(window.__calls.indexOf('palm://com.palm.connectionmanager/getstatus') >= 0)"),
          QStringLiteral("true"));
    check("and says its stage is ready", js("String(window.__ready === true)"), QStringLiteral("true"));

    // What came back is on screen, drawn by the kit's own elements.
    waitFor([&]() { return js(kRowTitles).contains("ZBook"); }, 5000);
    check("what came back is shown in HP's rows", js(kRowTitles),
          QStringLiteral("ZBook|webOS-CE-3.0.5|abc|Offline"));
    check("the header is the kit's", js("document.querySelector('hp-header').shadowRoot.querySelector('.hp-header span').textContent"),
          QStringLiteral("Template"));
    check("and it is styled by hp.css, not by the browser",
          js("getComputedStyle(document.querySelector('hp-header').shadowRoot.querySelector('.hp-header')).height"),
          QStringLiteral("48px"));
    check("with no spinner left running", js("String(document.querySelectorAll('hp-spinner').length)"),
          QStringLiteral("0"));

    // A service that is not running: the card says so, and offers the way back.
    // The card asks when it is shown, so the failure is put in place and the
    // page loaded again -- which is what happens on a device when the card is
    // opened while the service is down.
    loaded = false;
    QFile failing(dir.filePath("index.html"));
    if (!failing.open(QIODevice::WriteOnly))
        return 1;
    failing.write("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
                  "<link rel=\"stylesheet\" href=\"hp.css\"></head><body><div id=\"card\"></div>"
                  "<script>");
    failing.write(kFakeBridge);
    failing.write("window.__answers['palm://com.palm.deviceprofile/getDeviceProfile'] ="
                  " { returnValue: false, errorText: 'com.palm.deviceprofile is not running' };");
    failing.write("</script><script src=\"main.js\"></script></body></html>");
    failing.close();
    card.mainFrame()->load(QUrl::fromLocalFile(dir.filePath("index.html")));
    if (!waitFor([&]() { return loaded; }, 20000))
        return 1;

    waitFor([&]() { return !js("document.querySelector('.hp-error') ? document.querySelector('.hp-error').textContent : ''").isEmpty(); }, 5000);
    check("a service that is not running is said, not swallowed",
          js("document.querySelector('.hp-error').textContent"),
          QStringLiteral("com.palm.deviceprofile is not running"));

    // Press "Try again": the card asks once more, and this time it is answered.
    js("window.__answers['palm://com.palm.deviceprofile/getDeviceProfile'] ="
       " { returnValue: true, deviceInfo: { deviceModel: 'Answered', softwareVersion: '', nduId: '' } };"
       "window.__calls = [];"
       "document.querySelector('hp-button').shadowRoot.querySelector('button').click(); 1");
    waitFor([&]() { return js(kRowTitles).contains("Answered"); }, 5000);
    check("pressing Try again asks the bus once more", js("String(window.__calls.length)"), QStringLiteral("1"));
    check("and what comes back replaces the error", js(kRowTitles),
          QStringLiteral("Answered|unknown|unknown|Offline"));

    // The second screen, and the way back out of it: the card only closes once
    // there is nothing left to go back to.
    js("document.querySelectorAll('hp-row')[3].shadowRoot.querySelector('.hp-row-text').click(); 1");
    waitFor([&]() { return js("document.querySelector('hp-header').getAttribute('title')") == "Network"; }, 3000);
    check("a row opens the second screen", js("document.querySelector('hp-header').getAttribute('title')"),
          QStringLiteral("Network"));
    js("window.Mojo.handleGesture('back'); 1");
    waitFor([&]() { return js("document.querySelector('hp-header').getAttribute('title')") == "Template"; }, 3000);
    check("and the back gesture comes out of it",
          js("document.querySelector('hp-header').getAttribute('title')"), QStringLiteral("Template"));
    check("with the error gone", js("String(document.querySelectorAll('.hp-error').length)"), QStringLiteral("0"));

    return failures == 0 ? 0 : 1;
}
