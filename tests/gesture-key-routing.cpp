// Where a key goes when a page is painted inside another one.
//
// QWebPage::event() sends keys to whichever page was last pressed. That is what
// makes typing work: click a field inside the browser's content and the letters
// have to land there, not in the app's address bar. Nothing here changes that,
// and half this test exists to keep it that way.
//
// But a system gesture is not typing. The back gesture belongs to the app that
// owns the card, and the app's document is the host's -- that is where enyo's
// Gesture.js listens. MEASURED in the running browser, with the strip's back
// gesture driven by hand and a listener on every document:
//
//     [La Tomatina - Wikipedia] keydown keyCode=27 key="Escape"
//
// The escape reached the embedded content, which ignores it, and the browser's
// own document never saw one. So the back gesture did nothing, while the key
// was arriving correctly all along.
//
// sendKeyToHostPage is the other address. The caller picks it because only the
// caller knows which of the two kinds a key is; this pins that the two
// addresses really are different, in both directions.
//
// Runs headless:  ./gesture-key-routing -platform offscreen
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMouseEvent>

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
    std::printf("%-58s %-10s %s\n", what, qPrintable(got),
                ok ? "ok" : qPrintable("FAILED, wanted " + expected));
}

static const char kRecorder[] =
    "<html><body style='margin:0'>"
    "<script>window.__keys = 0; window.__last = '';"
    "document.addEventListener('keydown', function (e) {"
    "  window.__keys++; window.__last = String(e.keyCode);"
    "}, true);</script></body></html>";

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const QRect hole(100, 50, 200, 150);

    QWebPage host;
    host.setViewportSize(QSize(400, 300));
    bool hostLoaded = false;
    QObject::connect(&host, &QWebPage::loadFinished, &host, [&](bool) { hostLoaded = true; });
    host.mainFrame()->setHtml(QString::fromLatin1(kRecorder));

    QWebPage inner;
    bool innerLoaded = false;
    QObject::connect(&inner, &QWebPage::loadFinished, &inner, [&](bool) { innerLoaded = true; });
    inner.mainFrame()->setHtml(QString::fromLatin1(kRecorder));

    if (!waitFor([&]() { return hostLoaded && innerLoaded; }, 15000)) {
        std::printf("a page never finished loading\n");
        return 1;
    }

    host.embedPage(&inner, hole);
    waitFor([]() { return false; }, 1200);

    const auto innerSays = [&](const QString& code) {
        return inner.mainFrame()->evaluateJavaScript(code).toString();
    };
    const auto hostSays = [&](const QString& code) {
        return host.mainFrame()->evaluateJavaScript(code).toString();
    };

    // A press inside the hole is what hands the keyboard to the embedded page,
    // exactly as clicking into the browser's content does.
    const QPointF where(hole.center());
    QMouseEvent press(QEvent::MouseButtonPress, where, where,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    host.event(&press);
    QMouseEvent release(QEvent::MouseButtonRelease, where, where,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    host.event(&release);
    waitFor([&]() { return innerSays("String(window.__clicks || 1)") != "0"; }, 3000);

    // Typing, which must keep going to the page that was pressed.
    QKeyEvent typedDown(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
    host.event(&typedDown);
    waitFor([&]() { return innerSays("String(window.__keys)") != "0"; }, 5000);

    check("typing goes to the page that was pressed",
          innerSays("String(window.__keys)"), "1");
    check("and not to the host",
          hostSays("String(window.__keys)"), "0");

    // The gesture, which must go to the app's own document even though the
    // embedded page still owns the keyboard.
    QKeyEvent gestureDown(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier,
                          QString::fromLatin1("\x1b"));
    host.sendKeyToHostPage(&gestureDown);
    waitFor([&]() { return hostSays("String(window.__keys)") != "0"; }, 5000);

    check("a gesture key goes to the host page",
          hostSays("String(window.__keys)"), "1");
    check("with the code the DOM needs for back",
          hostSays("String(window.__last)"), "27");
    check("and the embedded page did not also get it",
          innerSays("String(window.__keys)"), "1");

    return failures == 0 ? 0 : 1;
}
