// A second web view, painted inside the card.
//
// The browser app draws its own chrome -- address bar, bookmarks, history --
// as an ordinary enyo page, and leaves the content area to
// <object type="application/x-palm-browser">. On a device that object was an
// NPAPI plugin (BrowserAdapter) blitting a buffer that another process
// (BrowserServer) had painted with its own WebKit. Chromium has no plugin
// socket at all now, so the object is inert and the content area comes up
// blank.
//
// The replacement needs no plugin and no second process. Every app's pixels
// already pass through QWebFrame::render() in this layer, which grabs the
// engine's view and blits it into the painter WebAppMgr aims at the shared
// buffer the shell reads (WindowedWebApp.cpp:377). So a second page rendered
// offscreen can be blitted into that same painter, at the rect the object
// occupies, and the shell never knows there were two.
//
// Measured before writing any of it: one full repaint of a 1024x768 card costs
// about 1 ms, and the cost scales with area (0.23 ms at 512x384, 4.52 ms at
// 2048x1536), so the grab is real work and a second view is affordable --
// roughly 2 ms against a 16.7 ms frame. See tests/grab-cost.cpp.
//
// This checks the one thing the whole design rests on: that what the embedded
// page painted actually lands inside the host's pixels, in the right place and
// nowhere else.

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QPainter>

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
    std::printf("%-52s %-22s %s\n", what, qPrintable(got),
                ok ? "ok" : qPrintable("FAILED, wanted " + expected));
}

// Solid colours, so a wrong offset shows up as the wrong answer rather than as
// a near miss. Anti-aliasing never touches the middle of a filled rect.
static QString colourAt(const QImage& image, int x, int y)
{
    if (x < 0 || y < 0 || x >= image.width() || y >= image.height())
        return QStringLiteral("outside the image");
    const QColor c = image.pixelColor(x, y);
    if (c.red() > 200 && c.green() < 60 && c.blue() < 60)
        return QStringLiteral("red");
    if (c.red() > 200 && c.green() > 200 && c.blue() > 200)
        return QStringLiteral("white");
    return QString("r%1 g%2 b%3").arg(c.red()).arg(c.green()).arg(c.blue());
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const QSize hostSize(400, 300);
    const QRect hole(100, 50, 200, 150);   // where the <object> sits

    QWebPage host;
    host.setViewportSize(hostSize);
    bool hostLoaded = false;
    QObject::connect(&host, &QWebPage::loadFinished, &host, [&](bool) { hostLoaded = true; });
    host.mainFrame()->setHtml(QStringLiteral(
        "<html><body style='margin:0;background:#ffffff'></body></html>"));

    QWebPage inner;
    bool innerLoaded = false;
    QObject::connect(&inner, &QWebPage::loadFinished, &inner, [&](bool) { innerLoaded = true; });
    inner.mainFrame()->setHtml(QStringLiteral(
        "<html><body style='margin:0;background:#ff0000'></body></html>"));

    if (!waitFor([&]() { return hostLoaded && innerLoaded; }, 15000)) {
        std::printf("a page never finished loading\n");
        return 1;
    }

    host.embedPage(&inner, hole);

    // Give both engines a frame to put up before reading any pixels back.
    waitFor([]() { return false; }, 1200);

    QImage surface(hostSize, QImage::Format_ARGB32_Premultiplied);
    surface.fill(Qt::black);   // so "nothing was painted" cannot pass as white

    QPainter painter(&surface);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    host.mainFrame()->render(&painter, QWebFrame::ContentsLayer,
                             QRect(QPoint(0, 0), hostSize));
    painter.end();

    // The host still paints everywhere it used to.
    check("above the hole, the host's own pixels",
          colourAt(surface, 200, 20), "white");
    check("to the left of the hole, the host's own pixels",
          colourAt(surface, 40, 120), "white");

    // And the embedded page lands inside it.
    check("the middle of the hole is the embedded page",
          colourAt(surface, hole.center().x(), hole.center().y()), "red");
    check("just inside the hole's top left corner",
          colourAt(surface, hole.left() + 3, hole.top() + 3), "red");
    check("just inside the hole's bottom right corner",
          colourAt(surface, hole.right() - 3, hole.bottom() - 3), "red");

    // Placed, not smeared: a wrong offset or an unclipped blit shows up here.
    check("just outside the hole's left edge",
          colourAt(surface, hole.left() - 4, hole.center().y()), "white");
    check("just outside the hole's bottom edge",
          colourAt(surface, hole.center().x(), hole.bottom() + 4), "white");

    // What the host has over the hole -- a menu the app opened -- stays on top.
    const QRect menu(120, 60, 40, 30);
    host.setEmbeddedCutouts(&inner, QRegion(menu));
    surface.fill(Qt::black);
    QPainter again(&surface);
    again.setCompositionMode(QPainter::CompositionMode_Source);
    host.mainFrame()->render(&again, QWebFrame::ContentsLayer, QRect(QPoint(0, 0), hostSize));
    again.end();
    check("under a cutout, the host's own pixels",
          colourAt(surface, menu.center().x(), menu.center().y()), "white");
    check("and the rest of the hole is still the embedded page",
          colourAt(surface, hole.center().x() + 40, hole.center().y() + 40), "red");

    return failures == 0 ? 0 : 1;
}
