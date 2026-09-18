// A card written without our runtime, in the engine that runs it.
//
// The claim this defends is #65's: the platform and the controls are usable
// from anything, and our renderer is optional. `apps/example-plain` is a card
// that uses neither `startCard`, nor `defineElement`, nor lit-html -- it builds
// its screen with `document.createElement` and connects to the device with
// `connectCard`. If that ever stops working, the claim is false and this test
// is where it is found out.
//
// enyo's mistake was not having layers, it was making the top one compulsory,
// which is why porting an HP app today means rewriting it. This is the check
// that we are not repeating it.
//
// Verified by mutation: take `useStyles(styles)` out of kit/kit.ts and the
// control is unstyled; take the `app.ready()` out of connectCard and the stage
// is never ready; break the paint seam and what the bus said never reaches the
// screen.

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
    std::printf("%-58s %-22s %s\n", what, qPrintable(got.left(22)),
                ok ? "ok" : qPrintable("FAILED, wanted " + expected));
}

// WebAppMgr's bridge, answering for com.palm.connectionmanager, plus a
// PalmSystem that records what the card told it.
static const char kFakeBridge[] = R"JS(
window.__calls = [];
window.__closed = false;
window.PalmServiceBridge = function () { this.onservicecallback = null; };
window.PalmServiceBridge.prototype.call = function (uri, payload) {
    var self = this;
    window.__calls.push(uri);
    if (uri.indexOf("getstatus") < 0) return;
    setTimeout(function () {
        if (self.onservicecallback) self.onservicecallback(JSON.stringify({
            returnValue: true, isInternetConnectionAvailable: true,
            wifi: { state: "connected", ssid: "GachWLAN", ipAddress: "192.168.1.9" }
        }));
    }, 0);
};
window.PalmServiceBridge.prototype.cancel = function () { this.onservicecallback = null; };
window.PalmSystem = { stageReady: function () { window.__ready = true; } };
window.close = function () { window.__closed = true; };
)JS";

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const QString built = QString::fromLocal8Bit(qgetenv("WEBOS_CARDS_BUILD"));
    if (built.isEmpty() || !QFile::exists(built + "/com.gachlab.app.plain/main.js")) {
        std::printf("SKIP: the cards are not built (tools/build-cards.sh)\n");
        return 77;
    }

    QTemporaryDir dir;
    const QString source = built + "/com.gachlab.app.plain";
    for (const QString& name : { QStringLiteral("main.js"), QStringLiteral("page.css"), QStringLiteral("theme-enyo.css") })
        QFile::copy(source + "/" + name, dir.filePath(name));
    QFile page(dir.filePath("index.html"));
    if (!page.open(QIODevice::WriteOnly))
        return 1;
    page.write("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
               "<link rel=\"stylesheet\" href=\"theme-enyo.css\"><link rel=\"stylesheet\" href=\"page.css\"></head>"
               "<body><div id=\"card\"></div><script>");
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

    // The platform half, which is all this card uses: it asked the bus, and it
    // told WebAppMgr the card is on screen.
    waitFor([&]() { return js("String(window.__ready === true)") == "true"; }, 5000);
    check("connectCard alone says the stage is ready", js("String(window.__ready === true)"),
          QStringLiteral("true"));
    check("and the card watched com.palm.connectionmanager",
          js("String(window.__calls.some(function (u) { return u.indexOf('connectionmanager') >= 0; }))"),
          QStringLiteral("true"));

    // The controls are real custom elements, and they were UPGRADED --
    // createElement of a tag nobody defined also puts an element in the DOM,
    // so the presence of the node proves only that main.js did not throw.
    check("the kit defined its elements, and the card's were upgraded",
          js("String(customElements.get('wos-row') !== undefined"
             " && document.querySelector('wos-row') instanceof customElements.get('wos-row'))"),
          QStringLiteral("true"));

    // A control reads its properties, not its light DOM. This is the mistake
    // the first version of this card made -- textContent on a wos-button,
    // which has no <slot>, so the button came out blank -- and the card that
    // exists to show how the kit is driven from outside has to get it right.
    check("a control shows what its property says, not its light DOM",
          js("document.querySelector('wos-button').shadowRoot.querySelector('button').textContent.trim()"),
          QStringLiteral("Close"));

    // THE ONE THIS TEST EXISTS FOR. Nothing in this card hands the kit a
    // stylesheet: importing the kit is what styles it. 2.6rem of page.css's
    // 20px root is the 52px HP's rows were. Unstyled, a div in a shadow root
    // has no min-height at all.
    waitFor([&]() { return js("document.querySelector('wos-row').shadowRoot ? 'y' : 'n'") == "y"; }, 3000);
    check("a control built by hand comes out styled, with no runtime",
          js("getComputedStyle(document.querySelector('wos-row').shadowRoot.querySelector('.wos-row')).minHeight"),
          QStringLiteral("52px"));

    // The paint seam: what the bus said reached the screen, drawn by the card's
    // own thirty lines of DOM rather than by ours.
    waitFor([&]() { return js("document.querySelector('wos-row').getAttribute('title')") == "Online"; }, 5000);
    check("what the bus said is on screen, drawn by the card itself",
          js("document.querySelector('wos-row').getAttribute('title')"), QStringLiteral("Online"));
    check("and so is the detail it built",
          js("document.querySelector('wos-row').getAttribute('detail')"),
          QStringLiteral("over wifi (GachWLAN)"));

    // A control's event reaches the card, and the card's app handle works --
    // both without our renderer in between.
    js("document.querySelector('wos-button').shadowRoot.querySelector('button').click(); 1");
    waitFor([&]() { return js("String(window.__closed)") == "true"; }, 3000);
    check("the kit's own event reaches the card, and app.close() closes it",
          js("String(window.__closed)"), QStringLiteral("true"));

    return failures == 0 ? 0 : 1;
}
