// The Wi-Fi settings card, com.palm.app.wifi, run as WebAppMgr runs it.
//
// The card is ours (components/wifi-app) on top of enyo's lib/wifi, which is
// HP's; what the user sees is the two together. So this loads the card's own
// index.html against the real enyo framework, through the same QtWebEngine
// compat layer WebAppMgr uses, with PalmSystem and PalmServiceBridge replaced
// by stand-ins that answer com.palm.wifi the way components/nm-connectionmanager
// does and record every call.
//
// Each scenario is a fresh page, launched with the parameters the system menu
// sends (see WifiLaunchParams.h):
//
//   the list        no target: the networks, the radio switch, the note
//   join            a secured network with no profile: its join screen
//   joined          the connected network: its address screen, with the
//                   BSSID and channel, and the address in the field
//   radio           the switch turns the radio off and waits for it
//   known networks  listed with their security, one forgotten by swipe
//   settings        When Device Sleeps, read, changed, and a refused change
//   help            the archived help site
//   relaunch        a card already open, sent to a network by the menu
//
// Also caught here, because it broke the card outright: lib/wifi loads
// lib/networkproxy, which HP never released. Without components/enyo-lib-
// networkproxy the card's kinds are never defined and the page stays blank.

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>

#include <QWebFrame>
#include <QWebPage>

#include <cstdio>
#include <functional>

static int g_failures = 0;

static void check(bool ok, const char* what, const QString& detail = QString())
{
    std::printf("  %-62s %s%s\n", what, ok ? "OK" : "<-- FAIL",
                ok || detail.isEmpty() ? "" : qPrintable("  (" + detail + ")"));
    if (!ok)
        ++g_failures;
}

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

// ---------------------------------------------------------------------------
// The services, as the card sees them.

class FakeServices;

class FakeBridge : public QObject {
    Q_OBJECT
public:
    FakeBridge(FakeServices* services) : m_services(services) {}
    Q_INVOKABLE int call(const QString& uri, const QString& payload);
    Q_INVOKABLE void cancel() { m_cancelled = true; }
    void reply(const QString& body)
    {
        if (!m_cancelled)
            Q_EMIT response(body);
    }
    QString uri;
Q_SIGNALS:
    void response(const QString& body);
private:
    FakeServices* m_services;
    bool m_cancelled = false;
};

class FakeServices : public QObject {
    Q_OBJECT
public:
    // What getstatus answers and pushes.
    QString status = QStringLiteral(
        "{\"returnValue\":true,\"subscribed\":true,\"status\":\"connectionStateChanged\","
        "\"networkInfo\":{\"connectState\":\"ipConfigured\",\"ssid\":\"Casa\",\"profileId\":10,"
        "\"signalBars\":3,\"signalLevel\":80,\"ipAddress\":\"10.20.30.99\"},"
        "\"apInfo\":{\"bssid\":\"AA:BB:CC:DD:EE:FF\",\"channel\":6}}");
    QString profileList = QStringLiteral(
        "{\"returnValue\":true,\"profileList\":["
        "{\"wifiProfile\":{\"profileId\":10,\"ssid\":\"Casa\"}},"
        "{\"wifiProfile\":{\"profileId\":13,\"ssid\":\"Oficina\",\"security\":{\"securityType\":\"wpa-personal\"}}}]}");
    QString sleepMode = QStringLiteral("enable");
    bool refuseSleepMode = false;
    QString refuseConnect;     // errorText for connect, or empty to accept
    bool refuseRadio = false;
    QStringList calls;
    QList<FakeBridge*> statusSubscribers;

    Q_INVOKABLE QObject* create() { return new FakeBridge(this); }

    int count(const QString& prefix) const
    {
        int n = 0;
        for (const QString& c : calls)
            if (c.startsWith(prefix))
                ++n;
        return n;
    }

    QString last(const QString& prefix) const
    {
        for (int i = calls.size() - 1; i >= 0; --i)
            if (calls[i].startsWith(prefix))
                return calls[i];
        return QString();
    }

    void push(const QString& body)
    {
        status = body;
        for (FakeBridge* b : statusSubscribers)
            b->reply(body);
    }

    QString answer(FakeBridge* bridge, const QString& uri, const QString& payload)
    {
        const QString method = uri.section('/', -1);
        const QString service = uri.section('/', 2, 2);
        if (service == QLatin1String("com.palm.wifi")) {
            if (method == QLatin1String("getstatus")) {
                statusSubscribers << bridge;
                return status;
            }
            if (method == QLatin1String("getinfo"))
                return QStringLiteral("{\"returnValue\":true,\"wifiInfo\":{\"macAddress\":\"7C:21:4A:00:11:22\",\"wapi\":\"disabled\"}}");
            if (method == QLatin1String("findnetworks"))
                return QStringLiteral(
                    "{\"returnValue\":true,\"foundNetworks\":["
                    "{\"networkInfo\":{\"ssid\":\"Casa\",\"signalBars\":3,\"signalLevel\":80,\"profileId\":10,\"connectState\":\"ipConfigured\"}},"
                    "{\"networkInfo\":{\"ssid\":\"Oficina\",\"signalBars\":2,\"signalLevel\":65,\"securityType\":\"wpa-personal\"}},"
                    "{\"networkInfo\":{\"ssid\":\"Vieja <b>\",\"signalBars\":1,\"signalLevel\":20,\"securityType\":\"wep\"}}]}");
            if (method == QLatin1String("getprofile"))
                return QStringLiteral(
                    "{\"returnValue\":true,\"wifiProfile\":{\"profileId\":10,\"ssid\":\"Casa\",\"useStaticIp\":false},"
                    "\"ipInfo\":{\"ip\":\"10.20.30.99\",\"subnet\":\"255.255.255.0\",\"gateway\":\"10.20.30.1\",\"dns1\":\"10.20.30.1\"}}");
            if (method == QLatin1String("getprofilelist"))
                return profileList;
            if (method == QLatin1String("deleteprofile")) {
                const int id = QJsonDocument::fromJson(payload.toUtf8()).object().value("profileId").toInt();
                QJsonObject list = QJsonDocument::fromJson(profileList.toUtf8()).object();
                QJsonArray kept;
                for (const QJsonValue& v : list.value("profileList").toArray())
                    if (v.toObject().value("wifiProfile").toObject().value("profileId").toInt() != id)
                        kept.append(v);
                list.insert("profileList", kept);
                profileList = QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact));
                return QStringLiteral("{\"returnValue\":true}");
            }
            if (method == QLatin1String("connect") && !refuseConnect.isEmpty())
                return QStringLiteral("{\"returnValue\":false,\"errorText\":\"%1\"}").arg(refuseConnect);
            if (method == QLatin1String("setstate") && refuseRadio)
                return QStringLiteral("{\"returnValue\":false,\"errorText\":\"not allowed\"}");
            if (method == QLatin1String("setstate") || method == QLatin1String("connect"))
                return QStringLiteral("{\"returnValue\":true}");
        }
        if (service == QLatin1String("com.palm.connectionmanager") && method == QLatin1String("getWakeOnWiFiMode"))
            return QStringLiteral("{\"returnValue\":true,\"mode\":\"%1\"}").arg(sleepMode);
        if (service == QLatin1String("com.palm.connectionmanager") && method == QLatin1String("setWakeOnWiFiMode")) {
            if (refuseSleepMode)
                return QStringLiteral("{\"returnValue\":false,\"errorText\":\"could not save\"}");
            sleepMode = QJsonDocument::fromJson(payload.toUtf8()).object().value("mode").toString();
            return QStringLiteral("{\"returnValue\":true,\"mode\":\"%1\"}").arg(sleepMode);
        }
        if (service == QLatin1String("com.palm.applicationManager") && method == QLatin1String("open"))
            return QStringLiteral("{\"returnValue\":true}");
        if (service == QLatin1String("com.palm.connectionmanager"))
            return QStringLiteral("{\"returnValue\":true,\"subscribed\":true,\"isInternetConnectionAvailable\":true,"
                                  "\"wifi\":{\"state\":\"connected\",\"onInternet\":\"yes\"}}");
        return QStringLiteral("{\"returnValue\":false,\"errorText\":\"not in this test\"}");
    }
};

int FakeBridge::call(const QString& callUri, const QString& payload)
{
    uri = callUri;
    m_services->calls << callUri.section('/', 2) + ' ' + payload;
    const QString body = m_services->answer(this, callUri, payload);
    // Answered later, as the bus would -- and the status last. Nothing on the
    // bus orders the replies, and the card must not rely on knowing the joined
    // network before a profile it asked for arrives: lib/wifi reads
    // joinedNetwork.profileId when it does.
    const int delay = callUri.endsWith(QLatin1String("/getstatus")) ? 400 : 5;
    QTimer::singleShot(delay, this, [this, body]() { reply(body); });
    return 1;
}

// Stands in for WebAppMgr's PalmSystem: launch parameters, and a no-op for
// every other member enyo reaches for. Published the way SysMgrWebBridge
// publishes the service bridge, whose constructor shim is copied here.
static QString bootScript(const QString& launchParams)
{
    QString params = launchParams;
    params.replace('\\', "\\\\").replace('\'', "\\'");
    return QStringLiteral(R"JS(
(function () {
    var state = { launchParams: '%1', identifier: 'com.palm.app.wifi 1000', locale: 'en_us',
                  localeRegion: 'us', phoneRegion: 'us', timeFormat: 'HH12', isActivated: true,
                  screenOrientation: 'up', windowOrientation: 'up', deviceInfo: '{}' };
    window.PalmSystem = new Proxy(state, {
        get: function (t, k) { return (k in t) ? t[k] : function () {}; },
        set: function (t, k, v) { t[k] = v; return true; }
    });
    function PalmServiceBridge() {
        this.__native = PalmServiceBridgeFactory.create();
        var self = this;
        this.__native.response.connect(function (body) { if (self.__cb) self.__cb(body); });
    }
    PalmServiceBridge.prototype.call = function (url, payload) { return this.__native.call(url, payload); };
    PalmServiceBridge.prototype.cancel = function () { this.__native.cancel(); };
    Object.defineProperty(PalmServiceBridge.prototype, "onservicecallback", {
        set: function (fn) { this.__cb = fn; }, get: function () { return this.__cb; }
    });
    window.PalmServiceBridge = PalmServiceBridge;
})();
)JS").arg(params);
}

// ---------------------------------------------------------------------------
// The installed layout, rebuilt from the tree: the framework with its libraries
// -- ours beside HP's -- and the card with its index.html pointing at it.

static bool link(const QString& target, const QString& name)
{
    return QFile::link(target, name);
}

static QString buildTree(const QTemporaryDir& dir, const QString& source)
{
    const QString fw = source + "/components/enyo-1.0/framework";
    const QString root = dir.path();
    QDir().mkpath(root + "/framework/lib");
    link(fw + "/enyo.js", root + "/framework/enyo.js");
    link(fw + "/build", root + "/framework/build");
    for (const QString& lib : QDir(fw + "/lib").entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        link(fw + "/lib/" + lib, root + "/framework/lib/" + lib);
    if (!qEnvironmentVariableIsSet("WIFI_CARD_WITHOUT_NETWORKPROXY"))
        link(source + "/components/enyo-lib-networkproxy", root + "/framework/lib/networkproxy");

    const QString app = source + "/components/wifi-app";
    QDir().mkpath(root + "/app");
    for (const QString& entry : QDir(app).entryList(QDir::AllEntries | QDir::NoDotAndDotDot))
        if (entry != QLatin1String("index.html"))
            link(app + "/" + entry, root + "/app/" + entry);
    QFile in(app + "/index.html");
    QFile out(root + "/app/index.html");
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly))
        return QString();
    QString html = QString::fromUtf8(in.readAll());
    html.replace(QStringLiteral("/usr/palm/frameworks/enyo/0.10/framework/enyo.js"),
                 QStringLiteral("../framework/enyo.js"));
    out.write(html.toUtf8());
    return root + "/app/index.html";
}

// ---------------------------------------------------------------------------

struct Card {
    QWebPage page;
    FakeServices services;
    bool ready = false;

    Card(const QString& index, const QString& launchParams)
    {
        QObject::connect(page.mainFrame(), &QWebFrame::javaScriptWindowObjectCleared, [this, launchParams]() {
            page.mainFrame()->addToJavaScriptWindowObject("PalmServiceBridgeFactory", &services);
            page.mainFrame()->evaluateJavaScript(bootScript(launchParams));
        });
        page.setViewportSize(QSize(1024, 768));
        page.mainFrame()->load(QUrl::fromLocalFile(index));
        ready = waitFor([this]() { return js("!!(window.enyo && enyo.$ && enyo.$.wifiApp)") == "true"; }, 30000);
    }

    QString js(const QString& code)
    {
        return page.mainFrame()->evaluateJavaScript(code).toString();
    }

    bool until(const QString& condition, int ms = 5000)
    {
        return waitFor([&]() { return js(condition) == "true"; }, ms);
    }
};

static QString launchWith(const QString& ssid, const QString& security, int profileId, const QString& state)
{
    QJsonObject target{{"ssid", ssid}, {"securityType", security}};
    if (!state.isEmpty()) {
        target.insert("profileId", profileId);
        target.insert("connectState", state);
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"target", target}}).toJson(QJsonDocument::Compact));
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const QString source = QString::fromLocal8Bit(qgetenv("WEBOS_SOURCE_DIR"));
    if (source.isEmpty()) {
        std::printf("WEBOS_SOURCE_DIR is not set\n");
        return 2;
    }
    QTemporaryDir dir;
    const QString index = buildTree(dir, source);
    if (index.isEmpty()) {
        std::printf("could not lay out the card\n");
        return 2;
    }

    std::printf("the list\n");
    {
        Card card(index, QString());
        check(card.ready, "the card's kinds are defined and rendered");
        if (!card.ready)
            return 1;
        check(card.until("enyo.$.wifiApp_config.isInNetworkView()"), "it opens on the network list");
        check(card.services.count("com.palm.wifi/findnetworks") >= 1, "which it asked com.palm.wifi for");
        check(card.until("enyo.$.wifiApp_config.data.length === 3"), "and filled with the networks found");
        check(card.until("document.body.textContent.indexOf('Vieja <b>') >= 0")
                  && card.js("document.body.innerHTML.indexOf('Vieja <b>')") == "-1",
              "a name with markup is shown as text");
        check(card.js("enyo.$.wifiApp_radioSwitch.getShowing() && enyo.$.wifiApp_radioSwitch.getState()") == "true",
              "the radio switch is shown, on");
        check(card.js("enyo.$.wifiApp_autoJoinNote.getShowing()") == "true", "with the note about known networks");
        check(card.js("enyo.$.wifiApp_caption.getShowing() && enyo.$.wifiApp_caption.getContent() === ''") == "true",
              "and an empty caption line over the list, as on the phone");
        check(card.services.count("com.palm.wifi/getprofile ") == 0, "nothing opened without a target");
        check(card.js("(function () { var p = enyo.$.wifiApp_pane.hasNode(), s = enyo.$.wifiApp_scroller.hasNode();"
                      " return p.offsetTop + p.offsetHeight <= s.clientHeight; })()") == "true",
              "the card fits its scroller, so there is nothing to scroll",
              card.js("(function () { var p = enyo.$.wifiApp_pane.hasNode();"
                      " return p.offsetTop + '+' + p.offsetHeight + ' in ' + enyo.$.wifiApp_scroller.hasNode().clientHeight; })()"));
        check(card.js("enyo.$.wifiApp_config.hasNode().getBoundingClientRect().top"
                      " - enyo.$.wifiApp_scroller.hasNode().getBoundingClientRect().top") == "29",
              "with the list where it was: 23px, then the empty caption line",
              card.js("enyo.$.wifiApp_config.hasNode().getBoundingClientRect().top"
                      " - enyo.$.wifiApp_scroller.hasNode().getBoundingClientRect().top"));
    }

    std::printf("join\n");
    {
        Card card(index, launchWith("Oficina", "wpa-personal", 0, QString()));
        check(card.until("enyo.$.wifiApp_config.isInSecurityView()"), "a secured network opens on its join screen");
        check(card.js("enyo.$.wifiApp_caption.getContent()") == "Join Oficina", "titled with its name",
              card.js("enyo.$.wifiApp_caption.getContent()"));
        check(card.js("enyo.$.wifiApp_radioSwitch.getShowing()") == "false", "with the radio switch out of the way");
        card.js("enyo.$.wifiApp_config_joinPassword.setValue('correcthorse');"
                "enyo.$.wifiApp_config.joinInfoChanged();"
                "enyo.$.wifiApp_config_joinButton.hasNode().click();");
        check(waitFor([&]() { return card.services.count("com.palm.wifi/connect") == 1; }, 5000),
              "signing in asks com.palm.wifi to connect");
        const QJsonObject sent = QJsonDocument::fromJson(
            card.services.last("com.palm.wifi/connect").section(' ', 1).toUtf8()).object();
        check(sent.value("ssid").toString() == "Oficina"
                  && sent.value("security").toObject().value("securityType").toString() == "wpa-personal"
                  && sent.value("security").toObject().value("simpleSecurity").toObject()
                         .value("passKey").toString() == "correcthorse",
              "with the name, the security and the password",
              card.services.last("com.palm.wifi/connect"));

        card.services.push(QStringLiteral(
            "{\"returnValue\":true,\"subscribed\":true,\"status\":\"connectionStateChanged\","
            "\"networkInfo\":{\"connectState\":\"associationFailed\",\"ssid\":\"Oficina\",\"lastConnectError\":\"IncorrectPasskey\"}}"));
        check(card.until("enyo.$.wifiApp_config_joinMessage.getShowing()"), "a wrong password is reported");
        check(card.js("enyo.$.wifiApp_config_joinMessage.getContent()").contains("password"),
              "as a password problem", card.js("enyo.$.wifiApp_config_joinMessage.getContent()"));
    }

    std::printf("refusals\n");
    {
        Card card(index, launchWith("Oficina", "wpa-personal", 0, QString()));
        card.until("enyo.$.wifiApp_config.isInSecurityView()");
        card.services.refuseConnect = QStringLiteral("an enterprise network needs a user name");
        card.js("enyo.$.wifiApp_config_joinPassword.setValue('correcthorse');"
                "enyo.$.wifiApp_config.joinInfoChanged();"
                "enyo.$.wifiApp_config_joinButton.hasNode().click();");
        check(card.until("enyo.$.wifiApp_config_joinMessage.getShowing()"),
              "a refused join is shown on the join screen");
        check(card.js("enyo.$.wifiApp_config_joinMessage.getContent()") == "an enterprise network needs a user name",
              "with the service's reason", card.js("enyo.$.wifiApp_config_joinMessage.getContent()"));
        check(card.js("enyo.$.wifiApp_config_joinButton.getActive() || enyo.$.wifiApp_config_joinButton.getDisabled()") == "false",
              "and Sign In stops spinning, ready to try again");
    }
    {
        Card card(index, QString());
        card.until("enyo.$.wifiApp_config.isInNetworkView()");
        card.services.refuseRadio = true;
        card.js("enyo.$.wifiApp_radioSwitch.hasNode().click()");
        check(waitFor([&]() { return card.services.count("com.palm.wifi/setstate") == 1; }, 5000),
              "a radio switch the service refuses");
        check(card.until("!enyo.$.wifiApp_radioSwitch.getDisabled() && enyo.$.wifiApp_radioSwitch.getState()"),
              "goes back to where the radio is, and can be used again");
    }
    {
        Card card(index, launchWith("Casa", "", 10, "ipConfigured"));
        card.until("enyo.$.wifiApp_config.isInIpConfigView()");
        card.services.refuseConnect = QStringLiteral("not a gateway address: x");
        // "void": the call returns enyo's request object, which evaluateJavaScript
        // cannot turn into a value; it waits five seconds for nothing.
        card.js("void enyo.$.wifiApp_config_wifiIpConfig.$.Connect.call({profileId: 10, useStaticIp: true, "
                "ipInfo: {ip: '10.0.0.2', subnet: '255.0.0.0', gateway: 'x'}})");
        check(card.until("enyo.$.wifiApp_caption.getContent().indexOf('not a gateway address') >= 0"),
              "refused address settings are said above them", card.js("enyo.$.wifiApp_caption.getContent()"));
    }

    std::printf("an open network the menu could not join\n");
    {
        Card card(index, launchWith("Libre", "", 0, QString()));
        check(card.until("enyo.$.wifiApp_config.isInNetworkView()"), "opens on the list, not on a join screen");
    }

    std::printf("joined\n");
    {
        Card card(index, launchWith("Casa", "", 10, "ipConfigured"));
        check(card.until("enyo.$.wifiApp_config.isInIpConfigView()"), "the joined network opens on its address screen");
        check(card.services.last("com.palm.wifi/getprofile ").contains("\"profileId\":10"),
              "after reading its profile", card.services.last("com.palm.wifi/getprofile "));
        check(card.until("enyo.$.wifiApp_caption.getContent() === "
                         "'Connected to Casa. BSSID AA:BB:CC:DD:EE:FF, Channel 6.'"),
              "titled with the network, its BSSID and channel", card.js("enyo.$.wifiApp_caption.getContent()"));
        check(card.until("enyo.$.wifiApp_config_wifiIpConfig_ipField.getValue() === '10.20.30.99'"),
              "and the address is in its field",
              card.js("enyo.$.wifiApp_config_wifiIpConfig_ipField.getValue()"));
        check(card.js("enyo.$.wifiApp_config_wifiIpConfig_subnetField.getValue()") == "255.255.255.0",
              "so is the subnet");
    }

    std::printf("radio\n");
    {
        Card card(index, QString());
        card.until("enyo.$.wifiApp_config.isInNetworkView()");
        card.js("enyo.$.wifiApp_radioSwitch.hasNode().click()");
        check(waitFor([&]() { return card.services.count("com.palm.wifi/setstate") == 1; }, 5000)
                  && card.services.last("com.palm.wifi/setstate").contains("\"disabled\""),
              "the switch turns the radio off", card.services.last("com.palm.wifi/setstate"));
        check(card.js("enyo.$.wifiApp_radioSwitch.getDisabled()") == "true",
              "and waits, disabled, for the radio");
        card.services.push(QStringLiteral("{\"returnValue\":true,\"subscribed\":true,\"status\":\"serviceDisabled\"}"));
        check(card.until("enyo.$.wifiApp_config.isInOffView()"), "the radio off shows the off view");
        check(card.js("enyo.$.wifiApp_radioSwitch.getDisabled() || enyo.$.wifiApp_radioSwitch.getState()") == "false",
              "with the switch usable again, off");
        check(card.js("enyo.$.wifiApp_autoJoinNote.getShowing() || enyo.$.wifiApp_caption.getShowing()") == "false",
              "and neither the note nor the caption line");
    }

    std::printf("known networks\n");
    {
        Card card(index, QString());
        card.until("enyo.$.wifiApp_config.isInNetworkView()");
        card.js("enyo.$.wifiApp.showKnown()");
        check(card.until("enyo.$.wifiApp_knownGroup.getShowing()"), "the menu's Known Networks lists them");
        check(card.services.count("com.palm.wifi/getprofilelist") == 1, "from getprofilelist");
        check(card.until("enyo.$.wifiApp_knownGroup.hasNode().offsetParent !== null"
                         " && enyo.$.wifiApp_sleepMode.hasNode().offsetParent === null"),
              "on screen, and not the settings");
        const QString text = card.js("enyo.$.wifiApp_knownGroup.hasNode().textContent");
        check(text.contains("Casa") && text.contains("Open") && text.contains("Oficina")
                  && text.contains("WPA Personal"),
              "each with its security", text.simplified());
        check(card.js("enyo.$.wifiApp_radioSwitch.getShowing()") == "false"
                  && card.js("enyo.$.wifiApp_backButton.getShowing()") == "true",
              "the switch gives way to Back");

        card.js("enyo.$.wifiApp.forgetKnown(null, 1)");
        check(waitFor([&]() { return card.services.count("com.palm.wifi/deleteprofile") == 1; }, 5000)
                  && card.services.last("com.palm.wifi/deleteprofile").contains("\"profileId\":13"),
              "a swipe forgets that network", card.services.last("com.palm.wifi/deleteprofile"));
        check(waitFor([&]() { return card.services.count("com.palm.wifi/getprofilelist") == 2; }, 5000),
              "and the list is read again once it is gone");
        check(card.until("enyo.$.wifiApp_knownGroup.hasNode().textContent.indexOf('Oficina') < 0"),
              "without it");

        card.js("enyo.$.wifiApp.forgetKnown(null, 0)");
        check(waitFor([&]() { return card.services.count("com.palm.wifi/getprofilelist") == 3; }, 5000)
                  && card.until("enyo.$.wifiApp.known.length === 0"),
              "with none left, the list is empty");
        check(card.js("enyo.$.wifiApp_knownGroup.getShowing() && !enyo.$.wifiApp_noKnown.getShowing()") == "true",
              "and shown as an empty group, as on the phone");
        card.services.profileList = QStringLiteral("{\"returnValue\":false,\"errorText\":\"no\"}");
        card.js("enyo.$.wifiApp.showKnown()");
        check(card.until("enyo.$.wifiApp_noKnown.getShowing() && !enyo.$.wifiApp_knownGroup.getShowing()"),
              "a list that cannot be read says there are no known networks");

        card.js("enyo.$.wifiApp_backButton.hasNode().click()");
        check(card.until("enyo.$.wifiApp_radioSwitch.getShowing() && !enyo.$.wifiApp_backButton.getShowing()"),
              "Back returns to the list and its switch");
    }

    std::printf("settings\n");
    {
        Card card(index, QString());
        card.until("enyo.$.wifiApp_config.isInNetworkView()");
        card.services.sleepMode = QStringLiteral("disable");
        card.js("enyo.$.wifiApp.showSettings()");
        check(card.services.count("com.palm.connectionmanager/getWakeOnWiFiMode") == 1,
              "the menu's Settings reads the sleep mode");
        check(card.until("enyo.$.wifiApp_sleepMode.hasNode().offsetParent !== null"
                         " && enyo.$.wifiApp_config.hasNode().offsetParent === null"),
              "and is the view on screen");
        check(card.until("enyo.$.wifiApp_sleepMode.getValue() === 'disable'"),
              "and shows it", card.js("enyo.$.wifiApp_sleepMode.getValue()"));
        check(card.js("enyo.$.wifiApp_sleepNote.getContent()").startsWith("May provide better battery life"),
              "with its explanation", card.js("enyo.$.wifiApp_sleepNote.getContent()"));
        check(card.js("enyo.$.wifiApp_radioSwitch.getShowing()") == "false"
                  && card.js("enyo.$.wifiApp_backButton.getShowing()") == "true",
              "the switch gives way to Back");

        card.js("enyo.$.wifiApp_sleepMode.setValue('enable'); enyo.$.wifiApp.sleepModeChosen();");
        check(waitFor([&]() { return card.services.count("com.palm.connectionmanager/setWakeOnWiFiMode") == 1; }, 5000)
                  && card.services.last("com.palm.connectionmanager/setWakeOnWiFiMode").contains("\"mode\":\"enable\""),
              "choosing Keep Wi-Fi On sets it", card.services.last("com.palm.connectionmanager/setWakeOnWiFiMode"));
        check(card.until("enyo.$.wifiApp_sleepNote.getContent().indexOf('Best for prolonging') === 0"),
              "and the explanation follows the answer");

        card.services.refuseSleepMode = true;
        card.js("enyo.$.wifiApp_sleepMode.setValue('disable'); enyo.$.wifiApp.sleepModeChosen();");
        check(waitFor([&]() { return card.services.count("com.palm.connectionmanager/getWakeOnWiFiMode") == 2; }, 5000),
              "a refused change reads the mode again");
        check(card.until("enyo.$.wifiApp_sleepMode.getValue() === 'enable'"),
              "and the list goes back to it", card.js("enyo.$.wifiApp_sleepMode.getValue()"));
    }

    std::printf("help\n");
    {
        Card card(index, QString());
        card.until("enyo.$.wifiApp_config.isInNetworkView()");
        // The menu makes its items when it is opened, as the user would open it.
        card.js("enyo.$.wifiApp_appMenu.open()");
        const QString items = card.js(
            "(function () { var r = []; for (var k in enyo.$) if (enyo.$[k].owner === enyo.$.wifiApp"
            " && /MenuItem|HelpMenu/.test(String(enyo.$[k].kind))) r.push(enyo.$[k].caption); return r.join('|'); })()");
        check(items == "Settings|Known Networks|Help", "the menu is Settings, Known Networks, Help", items);
        card.js("(function () { for (var k in enyo.$) if (/HelpMenu$/.test(String(enyo.$[k].kind))) enyo.$[k].itemClick(); })()");
        check(waitFor([&]() { return card.services.count("com.palm.applicationManager/open") == 1; }, 5000)
                  && card.services.last("com.palm.applicationManager/open")
                         .contains("\"target\":\"https://help.webosarchive.org/en-us/\""),
              "Help opens the archived help site", card.services.last("com.palm.applicationManager/open"));
    }

    std::printf("relaunch\n");
    {
        Card card(index, QString());
        card.until("enyo.$.wifiApp_config.isInNetworkView()");
        card.js(QStringLiteral("PalmSystem.launchParams = '%1'; Mojo.relaunch();")
                    .arg(launchWith("Oficina", "wpa-personal", 0, QString())));
        check(card.until("enyo.$.wifiApp_config.isInSecurityView()"),
              "an open card sent to a secured network shows its join screen");
    }

    std::printf("\n%s\n", g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}

#include "wifi-card.moc"
