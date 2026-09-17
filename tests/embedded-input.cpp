// A click inside the hole reaches the page painted there.
//
// The shell sends a card's touches to WebAppMgr, which turns them into
// QMouseEvents in the card's own coordinates and hands them to
// QWebPage::event() (WindowedWebApp.cpp:405). That forwards them to the
// engine's widget -- the HOST page's widget, which is the only one it knows
// about. So the browser rendered its page and nothing could be clicked or
// scrolled in it: every touch landed on the page holding the hole, at a spot
// where there is nothing but an empty div.
//
// The events that carry a position have to be routed by that position: if it
// falls inside an embedded page's rect, it belongs to that page, translated
// into its own coordinates.
//
// This drives the same path the shell does -- a press and a release at a point
// inside the hole -- and asks the embedded page what it saw. The coordinates
// matter as much as the count: an event delivered untranslated would land at
// the host's coordinates inside the embedded page, which is a different place
// entirely and exactly the kind of off-by-a-rect mistake that looks like it
// works until something is near an edge.

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
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
    std::printf("%-54s %-14s %s\n", what, qPrintable(got),
                ok ? "ok" : qPrintable("FAILED, wanted " + expected));
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const QSize hostSize(400, 300);
    const QRect hole(100, 50, 200, 150);

    QWebPage host;
    host.setViewportSize(hostSize);
    bool hostLoaded = false;
    QObject::connect(&host, &QWebPage::loadFinished, &host, [&](bool) { hostLoaded = true; });
    host.mainFrame()->setHtml(QStringLiteral(
        "<html><body style='margin:0'>"
        "<script>window.__hostClicks = 0;"
        "document.addEventListener('mousedown', function () { window.__hostClicks++; }, true);"
        "</script></body></html>"));

    QWebPage inner;
    bool innerLoaded = false;
    QObject::connect(&inner, &QWebPage::loadFinished, &inner, [&](bool) { innerLoaded = true; });
    inner.mainFrame()->setHtml(QStringLiteral(
        "<html><body style='margin:0;background:#ff0000'>"
        "<script>window.__clicks = 0; window.__at = '';"
        "document.addEventListener('mousedown', function (e) {"
        "  window.__clicks++;"
        "  window.__at = Math.round(e.clientX) + ',' + Math.round(e.clientY);"
        "}, true);</script></body></html>"));

    if (!waitFor([&]() { return hostLoaded && innerLoaded; }, 15000)) {
        std::printf("a page never finished loading\n");
        return 1;
    }

    host.embedPage(&inner, hole);
    waitFor([]() { return false; }, 1200);

    // The middle of the hole, in the host's coordinates, which is what the
    // shell would send.
    const QPointF where(hole.center());
    const QPointF expected(where - hole.topLeft());

    QMouseEvent press(QEvent::MouseButtonPress, where, where,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    host.event(&press);
    QMouseEvent release(QEvent::MouseButtonRelease, where, where,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    host.event(&release);

    const auto innerSays = [&](const QString& code) {
        return inner.mainFrame()->evaluateJavaScript(code).toString();
    };
    const auto hostSays = [&](const QString& code) {
        return host.mainFrame()->evaluateJavaScript(code).toString();
    };

    waitFor([&]() { return innerSays("String(window.__clicks)") != "0"; }, 5000);

    check("the embedded page got the press",
          innerSays("String(window.__clicks)"), "1");
    check("in its own coordinates, not the host's",
          innerSays("String(window.__at)"),
          QString("%1,%2").arg(int(expected.x())).arg(int(expected.y())));
    check("and the host did not also get it",
          hostSays("String(window.__hostClicks)"), "0");

    // Outside the hole the host still gets everything, or the browser's own
    // address bar would stop working.
    const QPointF outside(20, 20);
    QMouseEvent pressOutside(QEvent::MouseButtonPress, outside, outside,
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    host.event(&pressOutside);
    waitFor([&]() { return hostSays("String(window.__hostClicks)") != "0"; }, 5000);

    check("a press outside the hole stays with the host",
          hostSays("String(window.__hostClicks)"), "1");
    check("and the embedded page did not see it",
          innerSays("String(window.__clicks)"), "1");

    // A menu the app opened over the hole takes the presses that land on it.
    const QRect menu(hole.left() + 10, hole.top() + 10, 40, 30);
    host.setEmbeddedCutouts(&inner, QRegion(menu));
    const QPointF onMenu(menu.center());
    QMouseEvent pressMenu(QEvent::MouseButtonPress, onMenu, onMenu,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    host.event(&pressMenu);
    waitFor([&]() { return hostSays("String(window.__hostClicks)") != "1"; }, 5000);
    check("a press on a cutout goes to the host",
          hostSays("String(window.__hostClicks)"), "2");
    check("and not to the page under it",
          innerSays("String(window.__clicks)"), "1");

    return failures == 0 ? 0 : 1;
}
