// The Wi-Fi card, in the engine that runs it.
//
// components/cards/test covers what the card decides -- which screen is up,
// when a join can be attempted, what a failure says -- with no browser in
// sight. This one is the other half: the built bundle, the custom elements and
// the theme, loaded the way WebAppMgr loads them, against a PalmServiceBridge
// that answers the way com.palm.wifi does.
//
// So what is checked here is only what the library's tests cannot reach: that
// the card really asks the bus, that a reply pushed into a live subscription
// changes what is on screen, that a tap on a row sends the call HP's service
// expects, and that a launch target opens the screen it names.
//
// Verified by mutation: with the subscription not renewed, with the join
// payload flattened, or with the target ignored, this turns red.

#include <QApplication>
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
    std::printf("%-58s %-30s %s\n", what, qPrintable(got.left(30)),
                ok ? "ok" : qPrintable("FAILED, wanted " + expected));
}

// WebAppMgr's bridge, as the card finds it on the page. It keeps every bridge
// that is still listening, so the test can push a reply into a subscription the
// way a service does -- which is the part of com.palm.wifi that matters most
// here: the card is told about the radio and the join, it does not ask.
static const char kFakeBridge[] = R"JS(
window.__calls = [];
window.__live = [];
window.__answers = {};
window.PalmServiceBridge = function () { this.onservicecallback = null; this.uri = ""; };
window.PalmServiceBridge.prototype.call = function (uri, payload) {
    var self = this;
    this.uri = uri;
    this.payload = payload;
    window.__calls.push({ uri: uri, payload: payload });
    if (window.__live.indexOf(this) < 0) window.__live.push(this);
    var answer = window.__answers[uri];
    if (answer === undefined) return;
    setTimeout(function () {
        if (self.onservicecallback) self.onservicecallback(JSON.stringify(answer));
    }, 0);
};
window.PalmServiceBridge.prototype.cancel = function () {
    this.onservicecallback = null;
    var at = window.__live.indexOf(this);
    if (at >= 0) window.__live.splice(at, 1);
};

// What the service sends without being asked again.
window.__push = function (uri, reply) {
    var sent = 0;
    window.__live.forEach(function (bridge) {
        if (bridge.uri === uri && bridge.onservicecallback) {
            bridge.onservicecallback(JSON.stringify(reply));
            sent++;
        }
    });
    return sent;
};
window.__count = function (uri) {
    return window.__calls.filter(function (c) { return c.uri === uri; }).length;
};
window.__last = function (uri) {
    var seen = window.__calls.filter(function (c) { return c.uri === uri; });
    return seen.length ? seen[seen.length - 1].payload : "";
};

window.PalmSystem = { stageReady: function () { window.__ready = true; }, launchParams: "" };

var WIFI = "palm://com.palm.wifi/";
window.__answers[WIFI + "getstatus"] = {
    returnValue: true, status: "serviceEnabled",
    networkInfo: { ssid: "Casa", connectState: "ipConfigured", signalBars: 3, ipAddress: "10.20.30.99", profileId: 10 },
    apInfo: { bssid: "00:11:22:33:44:55", channel: 6 }
};
window.__answers[WIFI + "findnetworks"] = {
    returnValue: true,
    foundNetworks: [
        { networkInfo: { ssid: "Casa", signalBars: 3, securityType: "wpa-personal", profileId: 10, connectState: "ipConfigured" } },
        { networkInfo: { ssid: "Oficina", signalBars: 2, securityType: "wpa-personal" } },
        { networkInfo: { ssid: "Abierta", signalBars: 1, securityType: "none" } }
    ]
};
window.__answers[WIFI + "getprofilelist"] = {
    returnValue: true,
    profileList: [
        { wifiProfile: { profileId: 10, ssid: "Casa", security: { securityType: "wpa-personal" } } },
        { wifiProfile: { profileId: 11, ssid: "Abierta", security: { securityType: "none" } } }
    ]
};
window.__answers[WIFI + "getprofile"] = {
    returnValue: true,
    wifiProfile: { profileId: 10, ssid: "Casa", security: { securityType: "wpa-personal" }, useStaticIp: true },
    ipInfo: { ip: "10.20.30.99", subnet: "255.255.255.0", gateway: "10.20.30.1", dns1: "8.8.8.8", dns2: "" }
};
window.__answers[WIFI + "setstate"] = { returnValue: true };
window.__answers["palm://com.palm.connectionmanager/getWakeOnWiFiMode"] = { returnValue: true, mode: "disable" };
)JS";

// What the card shows: the rows' titles, out of the shadow roots the elements
// keep them in.
static const char kRowTitles[] = R"JS(
Array.prototype.slice.call(document.querySelectorAll("hp-row, hp-swipe-row"))
    .map(function (row) {
        var t = row.shadowRoot && row.shadowRoot.querySelector(".hp-row-title");
        return t ? t.textContent : "";
    }).filter(function (t) { return t; }).join("|")
)JS";

static const char kRowDetails[] = R"JS(
Array.prototype.slice.call(document.querySelectorAll("hp-row"))
    .map(function (row) {
        var t = row.shadowRoot && row.shadowRoot.querySelector(".hp-row-detail");
        return t ? t.textContent : "";
    }).filter(function (t) { return t; }).join("|")
)JS";

// The page the card is loaded in, with the bridge in place before its bundle
// runs -- which is the order WebAppMgr puts them in.
static bool writePage(const QString& path, const QString& extra)
{
    QFile page(path);
    if (!page.open(QIODevice::WriteOnly))
        return false;
    page.write("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
               "<link rel=\"stylesheet\" href=\"theme-enyo.css\">"
               "<link rel=\"stylesheet\" href=\"page.css\">"
               "<link rel=\"stylesheet\" href=\"wifi.css\"></head>"
               "<body><div id=\"card\"></div><script>");
    page.write(kFakeBridge);
    page.write(extra.toUtf8());
    page.write("</script><script src=\"main.js\"></script></body></html>");
    page.close();
    return true;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const QString built = QString::fromLocal8Bit(qgetenv("WEBOS_CARDS_BUILD"));
    if (built.isEmpty() || !QFile::exists(built + "/com.gachlab.app.wifi/main.js")) {
        std::printf("SKIP: the cards are not built (tools/build-cards.sh)\n");
        return 77;
    }

    QTemporaryDir dir;
    const QString source = built + "/com.gachlab.app.wifi";
    for (const QString& name : { QStringLiteral("main.js"), QStringLiteral("page.css"),
                                 QStringLiteral("theme-enyo.css"), QStringLiteral("wifi.css") })
        QFile::copy(source + "/" + name, dir.filePath(name));
    if (!writePage(dir.filePath("index.html"), QString()))
        return 1;

    QWebPage card;
    bool loaded = false;
    QObject::connect(&card, &QWebPage::loadFinished, &card, [&loaded]() { loaded = true; });
    const auto js = [&](const char* code) { return card.mainFrame()->evaluateJavaScript(code).toString(); };
    const auto open = [&](const QString& extra) {
        loaded = false;
        if (!writePage(dir.filePath("index.html"), extra))
            return false;
        card.mainFrame()->load(QUrl::fromLocalFile(dir.filePath("index.html")));
        return waitFor([&]() { return loaded; }, 20000);
    };

    if (!open(QString())) {
        std::printf("the card never loaded\n");
        return 1;
    }

    // --- the list -----------------------------------------------------------

    waitFor([&]() { return js(kRowTitles).contains("Casa"); }, 5000);
    check("the card watches the radio", js("String(window.__count('palm://com.palm.wifi/getstatus'))"),
          QStringLiteral("1"));
    check("and asks for the networks around it",
          js("String(window.__count('palm://com.palm.wifi/findnetworks') >= 1)"), QStringLiteral("true"));
    check("which are the rows, with Join Network under them", js(kRowTitles),
          QStringLiteral("Casa|Oficina|Abierta|Join Network"));
    check("and says its stage is ready", js("String(window.__ready === true)"), QStringLiteral("true"));

    // A reply pushed into the subscription, which is how the service says a
    // join is under way. Nothing is asked for again.
    js("window.__calls = []; window.__push('palm://com.palm.wifi/getstatus',"
       " { returnValue: true, status: 'serviceEnabled',"
       "   networkInfo: { ssid: 'Oficina', connectState: 'associating', signalBars: 2 } }); 1");
    waitFor([&]() { return js(kRowDetails).contains("CONNECTING"); }, 3000);
    check("what the service pushes reaches the screen", js(kRowDetails),
          QStringLiteral("CONNECTING..."));
    check("without the card asking anything again", js("String(window.__calls.length)"),
          QStringLiteral("0"));

    // --- joining ------------------------------------------------------------

    if (!open(QString()))
        return 1;
    waitFor([&]() { return js(kRowTitles).contains("Oficina"); }, 5000);
    js("document.querySelectorAll('hp-row')[1].shadowRoot.querySelector('.hp-row-text').click(); 1");
    waitFor([&]() { return js("String(document.querySelectorAll('hp-field').length)") != "0"; }, 3000);
    check("a secured network opens the join screen",
          js("String(document.querySelectorAll('hp-field').length)"), QStringLiteral("1"));
    check("with the network already named", js("document.querySelector('.wifi-caption') ? "
          "document.querySelector('.wifi-caption').textContent : ''"),
          QStringLiteral("Join Oficina"));

    js("var f = document.querySelector('hp-field').shadowRoot.querySelector('input');"
       "f.value = 'una clave'; f.dispatchEvent(new Event('input', { bubbles: true })); 1");
    waitFor([&]() { return js("String(document.querySelector('hp-activity-button').disabled)") != "true"; }, 3000);
    js("window.__calls = [];"
       "document.querySelector('hp-activity-button').shadowRoot.querySelector('button').click(); 1");
    waitFor([&]() { return js("String(window.__count('palm://com.palm.wifi/connect'))") == "1"; }, 5000);
    check("Sign In asks the service to connect",
          js("String(window.__count('palm://com.palm.wifi/connect'))"), QStringLiteral("1"));
    // The shape HP's service reads, which enyo's own library sent: the key is
    // nested under security.simpleSecurity, not beside the ssid.
    check("with the payload com.palm.wifi expects",
          js("(function () { var p = JSON.parse(window.__last('palm://com.palm.wifi/connect'));"
             "return [p.ssid, p.security && p.security.securityType,"
             "        p.security && p.security.simpleSecurity && p.security.simpleSecurity.passKey].join(','); })()"),
          QStringLiteral("Oficina,wpa-personal,una clave"));

    // The service refusing, in the card's own words rather than the bus's.
    js("window.__push('palm://com.palm.wifi/getstatus', { returnValue: true, status: 'serviceEnabled',"
       " networkInfo: { ssid: 'Oficina', connectState: 'associationFailed',"
       "                lastConnectError: 'IncorrectPassword' } }); 1");
    waitFor([&]() { return !js("document.querySelector('.hp-error') ? "
                               "document.querySelector('.hp-error').textContent : ''").isEmpty(); }, 3000);
    check("a wrong password is said, not swallowed",
          js("document.querySelector('.hp-error').textContent"),
          QStringLiteral("The username or password you entered is not correct. Try again."));

    // --- the radio ----------------------------------------------------------

    if (!open(QString()))
        return 1;
    waitFor([&]() { return js(kRowTitles).contains("Casa"); }, 5000);
    js("window.__calls = [];"
       "document.querySelector('hp-toggle').shadowRoot.querySelector('button').click(); 1");
    waitFor([&]() { return js("String(window.__count('palm://com.palm.wifi/setstate'))") == "1"; }, 5000);
    check("the switch asks the service to turn the radio off",
          js("window.__last('palm://com.palm.wifi/setstate')"),
          QStringLiteral("{\"state\":\"disabled\"}"));
    check("and waits for it rather than believing itself",
          js("String(document.querySelector('hp-toggle').disabled)"), QStringLiteral("true"));
    js("window.__push('palm://com.palm.wifi/getstatus',"
       " { returnValue: true, status: 'serviceDisabled' }); 1");
    waitFor([&]() { return js("String(document.querySelector('hp-toggle').disabled)") == "false"; }, 3000);
    check("until the radio says it is off", js("document.querySelector('.wifi-off') ? "
          "document.querySelector('.wifi-off').textContent : ''"),
          QStringLiteral("Wi-Fi is turned off."));

    // --- opened at a network ------------------------------------------------

    // The shell opens this card at a network when a notification is tapped;
    // what arrives is PalmSystem.launchParams, before the card has any status.
    if (!open(QStringLiteral("window.PalmSystem.launchParams = JSON.stringify("
                             "{ target: { ssid: 'Casa', profileId: 10, connectState: 'ipConfigured' } });")))
        return 1;
    waitFor([&]() { return js("String(window.__count('palm://com.palm.wifi/getprofile'))") != "0"; }, 5000);
    check("a target opens the network it names",
          js("String(window.__count('palm://com.palm.wifi/getprofile') >= 1)"), QStringLiteral("true"));
    check("asking for that profile", js("window.__last('palm://com.palm.wifi/getprofile')"),
          QStringLiteral("{\"profileId\":10}"));
    waitFor([&]() { return js("String(document.querySelectorAll('hp-field').length)") == "5"; }, 3000);
    check("and shows the address it holds",
          js("document.querySelectorAll('hp-field')[0].shadowRoot.querySelector('input').value"),
          QStringLiteral("10.20.30.99"));

    // --- the app menu -------------------------------------------------------

    if (!open(QString()))
        return 1;
    waitFor([&]() { return js(kRowTitles).contains("Casa"); }, 5000);
    js("window.PalmSystem.launchParams = JSON.stringify({ 'palm-command': 'open-app-menu' });"
       "window.Mojo.relaunch(); 1");
    waitFor([&]() { return js("String(document.querySelector('hp-app-menu').open)") == "true"; }, 3000);
    check("the shell's relaunch opens the card's menu",
          js("String(document.querySelector('hp-app-menu').open)"), QStringLiteral("true"));
    js("var items = document.querySelector('hp-app-menu').shadowRoot.querySelectorAll('.hp-menu-item');"
       "items[1].click(); 1");
    waitFor([&]() { return js("String(window.__count('palm://com.palm.wifi/getprofilelist'))") != "0"; }, 5000);
    check("Known Networks asks for the saved profiles",
          js("String(window.__count('palm://com.palm.wifi/getprofilelist'))"), QStringLiteral("1"));
    waitFor([&]() { return js(kRowTitles).contains("Casa"); }, 3000);
    check("and lists them, each one swipeable", js(kRowTitles), QStringLiteral("Casa|Abierta"));

    return failures == 0 ? 0 : 1;
}
