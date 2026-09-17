// A file the engine would download instead of showing reaches the page that
// asked for it, and the engine's own download is refused.
//
// On a device the browser plugin reported such a response to the app
// (mimeNotSupported) and the app downloaded it through
// com.palm.downloadmanager. QtWebEngine makes it a download of the profile
// every page shares, which nothing answered: the browser simply did nothing.
//
// Verified by mutation: without the emit, or without the cancel, this turns
// red.

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include <QWebEngineDownloadRequest>
#include <QWebEnginePage>
#include <QWebEngineProfile>
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

static void check(const char* what, bool ok, const QString& detail = QString())
{
    if (!ok)
        ++failures;
    std::printf("%-56s %s %s\n", what, ok ? "ok" : "FAILED", qPrintable(detail));
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QTemporaryDir dir;
    if (!dir.isValid())
        return 1;
    {
        QFile file(dir.filePath("archive.bin"));
        file.open(QIODevice::WriteOnly);
        file.write(QByteArray(2048, 'x'));
        QFile page(dir.filePath("page.html"));
        page.open(QIODevice::WriteOnly | QIODevice::Text);
        QTextStream(&page) << "<html><body><a id='get' href='archive.bin' download>get</a></body></html>\n";
    }

    QWebPage page;
    bool loaded = false;
    QObject::connect(&page, &QWebPage::loadFinished, &page, [&loaded]() { loaded = true; });
    page.mainFrame()->setUrl(QUrl::fromLocalFile(dir.filePath("page.html")));
    if (!waitFor([&]() { return loaded; }, 15000)) {
        std::printf("the page did not load\n");
        return 1;
    }

    QList<QPair<QUrl, QString>> requested;
    QObject::connect(&page, &QWebPage::downloadRequested, &page,
                     [&requested](const QUrl& url, const QString& mimeType) { requested.append({url, mimeType}); });

    // What happened to the engine's own download, seen after the shim's
    // handler: the shim connected first.
    QWebEngineDownloadRequest::DownloadState engineState = QWebEngineDownloadRequest::DownloadRequested;
    QObject::connect(page.enginePage()->profile(), &QWebEngineProfile::downloadRequested, &page,
                     [&engineState](QWebEngineDownloadRequest* download) { engineState = download->state(); });

    page.mainFrame()->evaluateJavaScript("document.getElementById('get').click(); 1");
    waitFor([&]() { return !requested.isEmpty(); }, 15000);

    check("the page is told what it asked for", requested.size() == 1, QString::number(requested.size()));
    if (!requested.isEmpty())
        check("with the file's URL", requested.first().first == QUrl::fromLocalFile(dir.filePath("archive.bin")),
              requested.first().first.toString());
    check("and the engine's own download is refused",
          engineState == QWebEngineDownloadRequest::DownloadCancelled, QString::number(int(engineState)));

    return failures == 0 ? 0 : 1;
}
