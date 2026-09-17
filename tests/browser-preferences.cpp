// The browser's Preferences reach the engine: Enable JavaScript, Block Popups
// and Accept Cookies. The view used to take all three and drop them (#46).
//
// Compiles WebAppMgr's BrowserViewAdapter against the compat layer, as the
// shell runs it. Accept Cookies is checked against a local HTTP server that
// sets a cookie and reports whether it came back.
//
// Verified by mutation: dropping any of the three setters, or letting the
// cookie filter pass web pages, turns this red.

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextStream>

#include <QWebFrame>
#include <QWebPage>

#include "BrowserViewAdapter.h"

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

static void check(const char* what, bool ok, const QString& detail = QString())
{
    if (!ok)
        ++failures;
    std::printf("%-58s %s %s\n", what, ok ? "ok" : "FAILED", qPrintable(detail));
}

// Answers every request with a page whose title says whether the request
// carried a cookie, and sets one.
class CookieServer : public QTcpServer
{
public:
    CookieServer()
    {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
                    const QByteArray request = socket->readAll();
                    const bool cookie = request.toLower().contains("\r\ncookie: seen=1");
                    const QByteArray body = QByteArray("<html><head><title>") + (cookie ? "cookie" : "none")
                        + "</title></head><body></body></html>";
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nSet-Cookie: seen=1; Path=/\r\n"
                                  "Cache-Control: no-store\r\nConnection: close\r\nContent-Length: "
                                  + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QTemporaryDir dir;
    if (!dir.isValid())
        return 1;
    {
        QFile page(dir.filePath("script.html"));
        page.open(QIODevice::WriteOnly | QIODevice::Text);
        QTextStream(&page) << "<html><head><title>static</title>"
                              "<script>document.title = 'scripted';</script></head><body></body></html>\n";
    }

    QWebPage host;
    BrowserViewAdapter view(&host);
    QString title;
    int loads = 0;
    QObject::connect(&view, &BrowserViewAdapter::titleChanged, &view, [&title](const QString& t) { title = t; });
    QObject::connect(&view, &BrowserViewAdapter::loadFinished, &view, [&loads](bool) { ++loads; });
    const auto load = [&](const QString& url, const QString& settle) {
        const int before = loads;
        title.clear();
        view.setUrl(url);
        waitFor([&]() { return loads > before && (settle.isEmpty() || title == settle); }, 15000);
        QElapsedTimer quiet;
        quiet.start();
        waitFor([&]() { return quiet.elapsed() > 300; }, 1000);
    };

    // Enable JavaScript
    const QString script = QUrl::fromLocalFile(dir.filePath("script.html")).toString();
    view.setEnableJavaScript(true);
    load(script, "scripted");
    check("with JavaScript on, the page's script runs", title == "scripted", title);
    view.setEnableJavaScript(false);
    load(script, "static");
    check("with JavaScript off, it does not", title == "static", title);
    view.setEnableJavaScript(true);

    // Block Popups
    view.setBlockPopups(true);
    check("Block Popups on stops pages opening windows", view.blocksPopups());
    view.setBlockPopups(false);
    check("and off lets them", !view.blocksPopups());

    // Accept Cookies
    CookieServer server;
    if (!server.listen(QHostAddress::LocalHost)) {
        std::printf("no local server\n");
        return 1;
    }
    const QString site = QString("http://127.0.0.1:%1/").arg(server.serverPort());
    view.setAcceptCookies(true);
    load(site, "none");
    load(site, "cookie");
    check("with cookies accepted, a site gets its cookie back", title == "cookie", title);
    check("and the view says so", view.acceptsCookies());

    view.setAcceptCookies(false);
    load(site + "?again", "");
    check("with cookies refused, it does not", title == "none", title);
    check("and the view says so", !view.acceptsCookies());

    view.setAcceptCookies(true);
    view.close();
    return failures == 0 ? 0 : 1;
}
