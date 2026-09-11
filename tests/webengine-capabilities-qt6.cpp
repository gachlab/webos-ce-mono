// Can QtWebEngine do what WebAppMgr asks of QtWebKit? Qt 6 only, no window.
//
// WebAppMgr draws every page into a buffer it shares with LunaSysMgr, feeds the
// page input events itself, and injects PalmSystem before the page's own scripts
// run. QtWebKit offered each of those directly. This checks the QtWebEngine
// counterpart of each one before an adapter is built on top of them:
//
//  1. render     a page drawn offscreen and read back into a QImage
//  2. input      a click sent to the view reaches the page's JavaScript, at the
//                position it was sent to
//  3. injection  an object the page can see before its own scripts run
//  4. resource   a synchronous read of a local file from JavaScript, which is
//                what palmGetResource() does
//  5. native     a synchronous call from JavaScript answered by C++ in this
//                process, through a URL scheme of our own: PalmSystem's
//                properties and methods return their value on the spot, and
//                QWebChannel, QtWebEngine's own bridge, is asynchronous only

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QPixmap>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QWebEnginePage>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QWebEngineView>
#include <QBuffer>

#include <cstdio>
#include <functional>

// Answers webos-bridge:///echo/<text> with "native:<text>", computed here.
class BridgeHandler : public QWebEngineUrlSchemeHandler
{
public:
    int calls = 0;
    void requestStarted(QWebEngineUrlRequestJob* job) override
    {
        ++calls;
        auto* body = new QBuffer(job);
        body->setData("native:" + job->requestUrl().path().section('/', 2).toUtf8());
        job->reply("text/plain", body);
    }
};

static bool waitFor(const std::function<bool()>& done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > timeoutMs)
            return false;
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return true;
}

static int report(const char* what, bool ok, const QString& detail)
{
    printf("%-10s : %-4s %s\n", what, ok ? "yes" : "NO", qPrintable(detail));
    return ok ? 0 : 1;
}

int main(int argc, char** argv)
{
    // A custom scheme has to be registered before the application exists.
    QWebEngineUrlScheme scheme("webos-bridge");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Path);
    // LocalScheme and SecureScheme are what let a file:// page -- which every
    // webOS app is -- call it; without them the request never reaches the handler.
    scheme.setFlags(QWebEngineUrlScheme::LocalScheme | QWebEngineUrlScheme::LocalAccessAllowed
                    | QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::CorsEnabled
                    | QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);

    QApplication app(argc, argv);
    BridgeHandler bridge;
    QWebEngineProfile::defaultProfile()->installUrlSchemeHandler("webos-bridge", &bridge);
    int failures = 0;

    QTemporaryDir dir;
    {
        QFile data(dir.filePath("resource.txt"));
        data.open(QIODevice::WriteOnly);
        data.write("from disk");
    }
    {
        QFile page(dir.filePath("page.html"));
        page.open(QIODevice::WriteOnly);
        page.write(R"HTML(<!doctype html>
<html><head><script>
  var injected = (typeof PalmSystem !== "undefined") ? PalmSystem.launchParams : "missing";
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "resource.txt", false);
  var resource = "failed";
  try { xhr.send(); resource = xhr.responseText; } catch (e) { resource = "threw " + e; }
  var bridge = new XMLHttpRequest();
  var native = "failed";
  try {
    // Three slashes: with Syntax::Path there is no host, and "//echo" would be one.
    bridge.open("GET", "webos-bridge:///echo/ping", false);
    bridge.send();
    native = bridge.responseText;
  } catch (e) { native = "threw " + e; }
  document.title = "loaded|" + injected + "|" + resource + "|" + native;
  document.addEventListener("click", function (e) {
    document.title = "clicked|" + e.clientX + "," + e.clientY;
  });
</script></head>
<body style="margin:0; background:#ff00c8; width:200px; height:200px"></body></html>
)HTML");
    }

    QWebEngineView view;
    view.setAttribute(Qt::WA_DontShowOnScreen);
    view.resize(200, 200);

    // 3. injection, registered before the page loads
    QWebEngineScript palm;
    palm.setName("PalmSystem");
    palm.setInjectionPoint(QWebEngineScript::DocumentCreation);
    palm.setWorldId(QWebEngineScript::MainWorld);
    palm.setSourceCode("window.PalmSystem = { launchParams: '{\"from\":\"native\"}' };");
    view.page()->scripts().insert(palm);

    bool loaded = false;
    QObject::connect(view.page(), &QWebEnginePage::loadFinished, [&](bool ok) { loaded = ok; });
    view.show();
    view.load(QUrl::fromLocalFile(dir.filePath("page.html")));

    if (!waitFor([&] { return loaded; }, 30000)) {
        printf("FAIL: the page did not load within 30 s\n");
        return 1;
    }
    waitFor([&] { return view.title().startsWith("loaded|"); }, 5000);
    const QStringList parts = view.title().split('|');

    failures += report("injection", parts.value(1) == "{\"from\":\"native\"}",
                       "PalmSystem.launchParams seen by the page: " + parts.value(1));
    failures += report("resource", parts.value(2) == "from disk",
                       "synchronous XHR returned: " + parts.value(2));
    failures += report("native", parts.value(3) == "native:ping",
                       QString("synchronous XHR to webos-bridge:// returned: %1 (handler calls: %2)")
                           .arg(parts.value(3)).arg(bridge.calls));

    // 1. render: poll, because the first frame arrives after loadFinished
    QColor pixel;
    const bool rendered = waitFor([&] {
        QImage shot = view.grab().toImage();
        if (shot.isNull())
            return false;
        pixel = shot.pixelColor(100, 100);
        return pixel == QColor("#ff00c8");
    }, 10000);
    failures += report("render", rendered, "pixel read back: " + pixel.name());

    // 2. input
    QWidget* target = view.focusProxy() ? view.focusProxy() : &view;
    QTest::mouseClick(target, Qt::LeftButton, Qt::NoModifier, QPoint(30, 40));
    const bool clicked = waitFor([&] { return view.title().startsWith("clicked|"); }, 5000);
    failures += report("input", clicked && view.title() == "clicked|30,40",
                       "title after the click: " + view.title());

    printf("%s\n", failures ? "FAIL" : "OK");
    return failures ? 1 : 0;
}
