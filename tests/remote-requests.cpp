// An app's own document can call a web service, as it could on a device.
//
// Chromium applies CORS to a file:// origin, so HP's apps' XMLHttpRequests to
// services that send no CORS headers were refused -- Just Type's search
// suggestions among them (#9). The compat layer routes a file:// document's
// requests to http(s) through its own scheme and makes them itself; web pages
// keep the web's rules.
//
// A local server here sends no CORS headers at all and records what arrives.
//
// Verified by mutation: without the rewrite, or with it applied to web pages
// too, this turns red.

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <QTextStream>

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
    std::printf("%-56s %-28s %s\n", what, qPrintable(got), ok ? "ok" : qPrintable("FAILED, wanted " + expected));
}

// Answers /suggest with an OpenSearch reply, /missing with a 404, and / with a
// page; keeps every request's text.
class Server : public QTcpServer
{
public:
    QStringList requests;

    Server()
    {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                    QByteArray& data = pending[socket];
                    data += socket->readAll();
                    const int end = data.indexOf("\r\n\r\n");
                    if (end < 0)
                        return;
                    const QRegularExpressionMatch m = QRegularExpression("content-length: *(\\d+)",
                        QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(data.left(end)));
                    if (m.hasMatch() && data.size() < end + 4 + m.captured(1).toInt())
                        return;
                    requests << QString::fromLatin1(data);
                    const QByteArray path = data.split(' ').value(1);
                    QByteArray status = "200 OK", type = "application/json", body;
                    if (path.startsWith("/suggest"))
                        body = "[\"gato\",[\"gatorade\",\"gatos\"]]";
                    else if (path.startsWith("/missing"))
                        status = "404 Not Found", body = "nope";
                    else
                        type = "text/html", body = "<html><body>site</body></html>";
                    socket->write("HTTP/1.1 " + status + "\r\nContent-Type: " + type + "\r\nConnection: close\r\n"
                                  "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                    pending.remove(socket);
                });
            }
        });
    }

private:
    QHash<QTcpSocket*, QByteArray> pending;
};

static QString run(QWebPage& page, const QString& script)
{
    page.mainFrame()->evaluateJavaScript("window.__result = undefined; " + script + "; 1");
    waitFor([&]() { return page.mainFrame()->evaluateJavaScript("window.__result === undefined").toBool() == false; }, 10000);
    return page.mainFrame()->evaluateJavaScript("String(window.__result)").toString();
}

static bool load(QWebPage& page, const QUrl& url)
{
    bool done = false;
    const auto link = QObject::connect(&page, &QWebPage::loadFinished, &page, [&done]() { done = true; });
    page.mainFrame()->setUrl(url);
    const bool ok = waitFor([&]() { return done; }, 15000);
    QObject::disconnect(link);
    return ok;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    Server server;
    if (!server.listen(QHostAddress::LocalHost))
        return 1;
    const QString base = QString("http://127.0.0.1:%1").arg(server.serverPort());

    QTemporaryDir dir;
    {
        QFile file(dir.filePath("app.html"));
        file.open(QIODevice::WriteOnly | QIODevice::Text);
        QTextStream(&file) << "<html><body>app</body></html>\n";
    }

    QWebPage app_page;
    if (!load(app_page, QUrl::fromLocalFile(dir.filePath("app.html")))) {
        std::printf("the app page did not load\n");
        return 1;
    }

    check("an app's XMLHttpRequest reaches a service without CORS",
          run(app_page, QString("var x = new XMLHttpRequest(); x.open('GET', '%1/suggest?q=gato');"
                                "x.onload = function () { window.__result = x.status + ' ' + JSON.parse(x.responseText)[1][0]; };"
                                "x.onerror = function () { window.__result = 'error'; }; x.send();").arg(base)),
          "200 gatorade");
    check("a synchronous one too",
          run(app_page, QString("var x = new XMLHttpRequest(); x.open('GET', '%1/suggest?q=s', false); x.send();"
                                "window.__result = x.status + ' ' + x.responseText.length;").arg(base)),
          "200 29");
    check("and fetch",
          run(app_page, QString("fetch('%1/suggest').then(function (r) { return r.json(); })"
                                ".then(function (j) { window.__result = j[1][1]; }, function () { window.__result = 'error'; })").arg(base)),
          "gatos");

    const int before = server.requests.size();
    check("a POST with its own header",
          run(app_page, QString("var x = new XMLHttpRequest(); x.open('POST', '%1/suggest'); x.setRequestHeader('X-Palm', 'yes');"
                                "x.onload = function () { window.__result = x.status; };"
                                "x.onerror = function () { window.__result = 'error'; }; x.send('body=1');").arg(base)),
          "200");
    const QString posted = server.requests.value(before);
    check("arrives with its method, header and body",
          QString("%1 %2 %3").arg(posted.startsWith("POST") ? "POST" : "?", posted.contains("x-palm: yes", Qt::CaseInsensitive) ? "header" : "?",
                                  posted.endsWith("body=1") ? "body" : "?"),
          "POST header body");

    check("an error status fails the request",
          run(app_page, QString("var x = new XMLHttpRequest(); x.open('GET', '%1/missing');"
                                "x.onload = function () { window.__result = 'loaded ' + x.status; };"
                                "x.onerror = function () { window.__result = 'error ' + x.status; }; x.send();").arg(base)),
          "error 0");

    // A web page keeps the web's rules: its own requests are not rerouted.
    QWebPage site;
    if (!load(site, QUrl(base + "/"))) {
        std::printf("the web page did not load\n");
        return 1;
    }
    check("a web page's request goes out as it wrote it",
          run(site, "var x = new XMLHttpRequest(); x.open('GET', '/suggest?q=w');"
                    "x.onload = function () { window.__result = x.responseURL.indexOf('webos-bridge') < 0 ? 'direct' : 'proxied'; };"
                    "x.onerror = function () { window.__result = 'error'; }; x.send();"),
          "direct");

    return failures == 0 ? 0 : 1;
}
