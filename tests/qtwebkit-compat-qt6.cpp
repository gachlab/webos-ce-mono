// components/qtwebkit-compat: QtWebKit's API as WebAppMgr uses it, on
// QtWebEngine. Qt 6 only, no window.
//
// Driven the way SysMgrWebBridge drives QtWebKit: objects added from
// javaScriptWindowObjectCleared must be visible to the page's first script, their
// properties and methods must answer synchronously, an object returned by a
// method (PalmServiceBridgeFactory.create()) must carry its signals to
// JavaScript, and the page must render, take input and answer the frame queries
// WindowedWebApp and AlertWebApp make.
//
// Built with -fno-rtti and through a QWebPage subclass, as WebAppMgr is: Qt 6's
// debug-build check on a signal connected to a member function dynamic_casts the
// receiver, and a class compiled without RTTI has no type_info to cast with.
// WebAppMgr segfaulted in loadProgress that way while this test, without the
// subclass, passed.

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QTemporaryDir>
#include <QTimer>

#include <QWebElement>
#include <QWebFrame>
#include <QWebPage>

#include <cstdio>
#include <functional>

class ServiceBridge : public QObject
{
    Q_OBJECT
public:
    Q_INVOKABLE void call(const QString& payload)
    {
        // Answered later, as a bus reply would be.
        QTimer::singleShot(10, this, [this, payload]() { Q_EMIT response("pong:" + payload); });
    }
Q_SIGNALS:
    void response(const QString& body);
};

class AppPage : public QWebPage
{
    Q_OBJECT
public:
    using QWebPage::QWebPage;
};

class Native : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString launchParams READ launchParams)
public:
    QString launchParams() const { return QStringLiteral("{\"from\":\"native\"}"); }
    Q_INVOKABLE QString greet(const QString& who) { return "hello " + who; }
    Q_INVOKABLE QObject* create() { return new ServiceBridge; }
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
    printf("%-24s : %-4s %s\n", what, ok ? "yes" : "NO", qPrintable(detail));
    return ok ? 0 : 1;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    int failures = 0;

    QTemporaryDir dir;
    {
        QFile page(dir.filePath("app.html"));
        page.open(QIODevice::WriteOnly);
        page.write(R"HTML(<!doctype html>
<html><head><script>
  var first = "first|" + Native.launchParams + "|" + greetFromHelper();
  document.title = first;
  var bridge = Native.create();
  bridge.response.connect(function (body) { document.title = "signal|" + body; });
  bridge.call("ping");
  document.addEventListener("click", function (e) { window.clickedAt = e.clientX + "," + e.clientY; });
</script></head>
<body style="margin:0; background:#ff00c8">
  <input id="field" type="email" x-palm-input-type="email" style="position:absolute; left:10px; top:10px; width:100px; height:20px">
  <div x-palm-popup-content style="position:absolute; left:20px; top:60px; width:50px; height:40px"></div>
</body></html>
)HTML");
    }

    AppPage page;
    page.setViewportSize(QSize(200, 200));
    Native native;
    QObject::connect(page.mainFrame(), &QWebFrame::javaScriptWindowObjectCleared, [&]() {
        page.mainFrame()->addToJavaScriptWindowObject("Native", &native);
        page.mainFrame()->evaluateJavaScript("function greetFromHelper() { return Native.greet('webOS'); }");
    });
    int repaints = 0;
    QObject::connect(&page, &QWebPage::repaintRequested, [&](const QRect&) { ++repaints; });
    bool loaded = false;
    QObject::connect(&page, &QWebPage::loadFinished, [&](bool ok) { loaded = ok; });

    page.mainFrame()->load(QUrl::fromLocalFile(dir.filePath("app.html")));
    if (!waitFor([&] { return loaded; }, 30000)) {
        printf("FAIL: the page did not load within 30 s\n");
        return 1;
    }

    // The title the first script set is overwritten by the signal soon after,
    // so read the variable the script kept.
    const QString first = page.mainFrame()->evaluateJavaScript("first").toString();
    failures += report("objects before scripts", first == "first|{\"from\":\"native\"}|hello webOS",
                       "the page's first script saw: " + first);

    const bool signalled = waitFor([&] { return page.mainFrame()->title() == "signal|pong:ping"; }, 5000);
    failures += report("signal to JavaScript", signalled, "title: " + page.mainFrame()->title());

    const QVariant sum = page.mainFrame()->evaluateJavaScript("1 + 2");
    failures += report("evaluateJavaScript", sum.toInt() == 3, "1 + 2 = " + sum.toString());

    QColor pixel;
    const bool rendered = waitFor([&] {
        QImage image(200, 200, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::black);
        QPainter painter(&image);
        page.mainFrame()->render(&painter, QWebFrame::ContentsLayer, QRegion(0, 0, 200, 200));
        painter.end();
        pixel = image.pixelColor(150, 150);
        return pixel == QColor("#ff00c8");
    }, 10000);
    failures += report("render", rendered, "pixel: " + pixel.name());
    failures += report("repaintRequested", repaints > 0, QString("emitted %1 times").arg(repaints));

    const QWebHitTestResult hit = page.mainFrame()->hitTestContent(QPoint(20, 20));
    failures += report("hitTestContent", hit.isContentEditable()
                                             && hit.element().attribute("x-palm-input-type") == "email",
                       QString("editable %1, x-palm-input-type %2")
                           .arg(hit.isContentEditable()).arg(hit.element().attribute("x-palm-input-type")));

    const QWebElement popup = page.mainFrame()->findFirstElement("[x-palm-popup-content]");
    failures += report("findFirstElement", !popup.isNull() && popup.geometry() == QRect(20, 60, 50, 40),
                       QString("geometry %1,%2 %3x%4").arg(popup.geometry().x()).arg(popup.geometry().y())
                           .arg(popup.geometry().width()).arg(popup.geometry().height()));

    // WebAppMgr hands the page heap-allocated events and never deletes them;
    // stack events here.
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(150, 150), QPointF(150, 150),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(150, 150), QPointF(150, 150),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    page.event(&press);
    page.event(&release);
    QString clickedAt;
    waitFor([&] {
        clickedAt = page.mainFrame()->evaluateJavaScript("window.clickedAt || ''").toString();
        return !clickedAt.isEmpty();
    }, 5000);
    failures += report("input through event()", clickedAt == "150,150", "click seen at: " + clickedAt);

    printf("%s\n", failures ? "FAIL" : "OK");
    return failures ? 1 : 0;
}

#include "qtwebkit-compat-qt6.moc"
