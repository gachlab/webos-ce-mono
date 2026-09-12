// What one repaint of a card costs.
//
// This is a measurement, not a test: it prints numbers and always exits 0.
//
// WindowedWebApp::paint() (WindowedWebApp.cpp:355) is the whole pixel path of
// every app. Its inner line is
//
//     page()->page()->mainFrame()->render(ctxt, QWebFrame::ContentsLayer, m_paintRect);
//
// and that render() is ours (qtwebkit_compat.cpp): it grabs the engine's view
// into a QPixmap and blits it into the painter WebAppMgr aims at the shared
// buffer the shell reads.
//
// Two things make the cost worth knowing before backing the browser's dead
// <object> with a second web view:
//
//   * QWebPage::followRenderSurface() emits repaintRequested once per frame of
//     QtWebEngine's scene graph, for the WHOLE viewport rather than a dirty
//     region, and WindowedWebApp's paint timer runs at start(0) -- no rate cap.
//   * render() grabs the whole view whatever the clip says.
//
// So a page inside the card would drive a full-card grab per frame. If that
// costs a handful of milliseconds the design is comfortable; if it costs tens,
// the second view has to be composited some other way.
//
// Usage: grab-cost [width height [frames]]

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QPainter>

#include <QWebFrame>
#include <QWebPage>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

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

// Something with real work in it: gradients, text and a transform, so the
// grab is not measuring an empty white page.
static const char kBusyPage[] =
    "<html><body style='margin:0'>"
    "<div style='width:100%;height:100%;background:linear-gradient(135deg,#123,#c51,#1a8)'>"
    "<div style='padding:20px;font:16px sans-serif;color:#fff;transform:rotate(-2deg)'>"
    "<h1>webOS</h1>"
    "<p>Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod"
    " tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam,"
    " quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo.</p>"
    "<p>Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod"
    " tempor incididunt ut labore et dolore magna aliqua.</p>"
    "</div></div></body></html>";

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const int width  = argc > 1 ? std::atoi(argv[1]) : 1024;
    const int height = argc > 2 ? std::atoi(argv[2]) : 768;
    const int frames = argc > 3 ? std::atoi(argv[3]) : 60;

    QWebPage page;
    page.setViewportSize(QSize(width, height));

    bool loaded = false;
    QObject::connect(&page, &QWebPage::loadFinished, &page, [&](bool) { loaded = true; });
    page.mainFrame()->setHtml(QString::fromLatin1(kBusyPage));

    if (!waitFor([&]() { return loaded; }, 15000)) {
        std::printf("the page never finished loading\n");
        return 0;
    }
    // Let the engine actually put a frame up before timing anything.
    waitFor([]() { return false; }, 1000);

    // The surface WindowedWebApp paints into is the shared buffer; an image of
    // the same size and format stands in for it here.
    QImage surface(width, height, QImage::Format_ARGB32_Premultiplied);
    const QRect paintRect(0, 0, width, height);

    std::vector<double> times;
    times.reserve(frames);

    for (int i = 0; i < frames; ++i) {
        // Keep the page busy so each grab has something new to read.
        page.mainFrame()->evaluateJavaScript(
            QString("document.body.style.opacity = %1;").arg(0.90 + (i % 10) * 0.01));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);

        QElapsedTimer timer;
        timer.start();

        QPainter painter(&surface);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.setClipRect(paintRect);
        painter.fillRect(paintRect, Qt::transparent);
        page.mainFrame()->render(&painter, QWebFrame::ContentsLayer, paintRect);
        painter.end();

        times.push_back(timer.nsecsElapsed() / 1e6);
    }

    std::sort(times.begin(), times.end());
    double total = 0;
    for (double t : times)
        total += t;

    const double mean = total / times.size();
    const double median = times[times.size() / 2];
    const double worst = times.back();
    const double best = times.front();

    std::printf("card %dx%d, %d repaints of the whole viewport\n", width, height, frames);
    std::printf("  best   %6.2f ms\n", best);
    std::printf("  median %6.2f ms  -> %.0f fps if nothing else ran\n",
                median, median > 0 ? 1000.0 / median : 0.0);
    std::printf("  mean   %6.2f ms\n", mean);
    std::printf("  worst  %6.2f ms\n", worst);
    std::printf("\n");
    std::printf("A second web view inside the card costs this much again, per frame,\n");
    std::printf("on top of whatever the page itself is doing.\n");

    return 0;
}
