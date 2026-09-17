// A web page's navigator.geolocation goes through com.palm.location.
//
// QtWebEngine asks its embedder whether a site may have the location and Qt
// Positioning where it is. WebAppMgr's GeolocationAdapter answers both from
// com.palm.location -- here a fake one -- so a site is refused when the
// service refuses it, and gets the service's position when it does not.
//
// Verified by mutation: without the policy, without the static plugin, or with
// the refusals let through, this turns red.

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <QWebFrame>
#include <QWebPage>

#include "GeolocationAdapter.h"

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
    std::printf("%-58s %-24s %s\n", what, qPrintable(got), ok ? "ok" : qPrintable("FAILED, wanted " + expected));
}

static const char kPage[] =
    "<html><body><script>"
    "window.result = '';"
    "function locate() {"
    "  window.result = '';"
    "  navigator.geolocation.getCurrentPosition("
    "    function (p) { window.result = p.coords.latitude + ',' + p.coords.longitude + ',' + p.coords.accuracy; },"
    "    function (e) { window.result = 'error ' + e.code; },"
    "    { timeout: 10000 });"
    "}"
    "</script></body></html>";

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // A secure context without certificates: localhost over HTTP.
    QTcpServer server;
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server]() {
        while (QTcpSocket* socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
                socket->readAll();
                const QByteArray body(kPage);
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: "
                              + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        }
    });
    if (!server.listen(QHostAddress::LocalHost))
        return 1;
    const QString origin = QStringLiteral("http://localhost:%1").arg(server.serverPort());

    // The fake com.palm.location: what it was asked, and what it answers.
    QStringList asked;
    QJsonObject answer;
    Geolocation::setDefault([&](const QString& method, const QJsonObject& payload, Geolocation::Reply reply) {
        QJsonObject shown = payload;
        shown.remove("accuracy");
        shown.remove("responseTime");
        shown.remove("maximumAge");
        asked << method + " " + QString::fromUtf8(QJsonDocument(shown).toJson(QJsonDocument::Compact));
        const QJsonObject now = answer;
        QTimer::singleShot(0, [reply, now]() { reply(now); });
        return std::function<void()>([] {});
    });
    const QJsonObject position{{"returnValue", true}, {"errorCode", 0}, {"latitude", 10.5}, {"longitude", -66.9},
                               {"horizAccuracy", 30}, {"vertAccuracy", -1}, {"altitude", -1}, {"heading", -1},
                               {"velocity", -1}, {"timestamp", 1700000000000.0}};

    // A document the engine refused keeps that answer, so each case opens its
    // own page.
    const auto locateIn = [&](QWebPage& page) {
        bool loaded = false;
        QObject::connect(&page, &QWebPage::loadFinished, &page, [&loaded]() { loaded = true; });
        page.mainFrame()->load(QUrl(origin + "/"));
        if (!waitFor([&]() { return loaded; }, 15000))
            return QStringLiteral("the page never loaded");
        const auto js = [&](const char* code) { return page.mainFrame()->evaluateJavaScript(code).toString(); };
        js("locate(); 1");
        waitFor([&]() { return !js("window.result").isEmpty(); }, 12000);
        return js("window.result");
    };
    const QString aboutSite = "getCurrentPosition {\"url\":\"" + origin + "/\"}";

    // The service refuses the site: the page hears PERMISSION_DENIED.
    answer = QJsonObject{{"returnValue", false}, {"errorCode", 8}, {"errorText", "The user did not allow the location"}};
    {
        QWebPage page;
        check("a site com.palm.location refuses is denied", locateIn(page), "error 1");
        check("the service was asked about the site", asked.value(0), aboutSite);
        check("and nothing else", QString::number(asked.size()), "1");
    }

    // The service allows it: the page gets the service's position.
    asked.clear();
    answer = position;
    {
        QWebPage page;
        check("a site it allows gets its position", locateIn(page), "10.5,-66.9,30");
        check("asked about the site first", asked.value(0), aboutSite);
        check("then for the position, without a url",
              asked.value(1).section(' ', 1), QStringLiteral("{\"subscribe\":true}"));
    }

    return failures == 0 ? 0 : 1;
}
