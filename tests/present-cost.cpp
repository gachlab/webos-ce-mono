// Before/after present cost for #79 phase 2.
//
// Measures one full-viewport card repaint into a surface the size of the
// buffer path, with:
//   WEBOS_GRAB_PRESENT=1  — old grab→drawPixmap into a QImage (shm stand-in)
//   default               — direct QWidget::render into the same QImage
//   --dmabuf              — direct render into a GBM dma-buf mapped surface
//
// Not a ctest: prints numbers and exits 0. Run by hand:
//   QT_QPA_PLATFORM=offscreen ./build/tests/present-cost 1024 768
//   WEBOS_GRAB_PRESENT=1 QT_QPA_PLATFORM=offscreen ./build/tests/present-cost 1024 768
//   QT_QPA_PLATFORM=offscreen ./build/tests/present-cost --dmabuf 1024 768

#include "dmabuf_window.h"

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
#include <cstring>
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

static const char kBusyPage[] =
    "<html><body style='margin:0'>"
    "<div style='width:100%;height:100%;background:linear-gradient(135deg,#123,#c51,#1a8)'>"
    "<div style='padding:20px;font:16px sans-serif;color:#fff;transform:rotate(-2deg)'>"
    "<h1>webOS</h1>"
    "<p>Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod"
    " tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam,"
    " quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo.</p>"
    "</div></div></body></html>";

struct Stats {
    double best = 0;
    double median = 0;
    double mean = 0;
    double worst = 0;
};

static Stats runTimed(QWebPage& page, QImage* surface, int frames)
{
    const QRect paintRect(0, 0, surface->width(), surface->height());
    std::vector<double> times;
    times.reserve(frames);

    for (int i = 0; i < frames; ++i) {
        page.mainFrame()->evaluateJavaScript(
            QString("document.body.style.opacity = %1;").arg(0.90 + (i % 10) * 0.01));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);

        QElapsedTimer timer;
        timer.start();

        QPainter painter(surface);
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

    Stats s;
    s.best = times.front();
    s.median = times[times.size() / 2];
    s.mean = total / times.size();
    s.worst = times.back();
    return s;
}

static void printStats(const char* label, int width, int height, int frames, const Stats& s)
{
    std::printf("%s  card %dx%d, %d repaints\n", label, width, height, frames);
    std::printf("  best   %6.2f ms\n", s.best);
    std::printf("  median %6.2f ms  -> %.0f fps if nothing else ran\n",
                s.median, s.median > 0 ? 1000.0 / s.median : 0.0);
    std::printf("  mean   %6.2f ms\n", s.mean);
    std::printf("  worst  %6.2f ms\n", s.worst);
}

int main(int argc, char** argv)
{
    bool useDmabuf = false;
    int argi = 1;
    if (argi < argc && std::strcmp(argv[argi], "--dmabuf") == 0) {
        useDmabuf = true;
        ++argi;
    }

    QApplication app(argc, argv);

    const int width = argi < argc ? std::atoi(argv[argi++]) : 1024;
    const int height = argi < argc ? std::atoi(argv[argi++]) : 768;
    const int frames = argi < argc ? std::atoi(argv[argi++]) : 60;

    QWebPage page;
    page.setViewportSize(QSize(width, height));

    bool loaded = false;
    QObject::connect(&page, &QWebPage::loadFinished, &page, [&](bool) { loaded = true; });
    page.mainFrame()->setHtml(QString::fromLatin1(kBusyPage));

    if (!waitFor([&]() { return loaded; }, 15000)) {
        std::printf("the page never finished loading\n");
        return 0;
    }
    waitFor([]() { return false; }, 1000);

    const char* mode = qEnvironmentVariableIntValue("WEBOS_GRAB_PRESENT") == 1
        ? "grab→drawPixmap"
        : "direct render";

    if (useDmabuf) {
        if (!dmabuf_window::available()) {
            std::printf("dma-buf path requested but no render node/GBM\n");
            return 0;
        }
        auto device = dmabuf_window::Device::openDefault();
        auto frame = dmabuf_window::Frame::create(device, static_cast<uint32_t>(width),
                                                  static_cast<uint32_t>(height));
        if (!frame) {
            std::printf("Frame::create failed\n");
            return 0;
        }
        uint32_t stride = 0;
        void* ptr = frame->mapWrite(&stride);
        if (!ptr) {
            std::printf("mapWrite failed\n");
            return 0;
        }
        QImage surface(static_cast<uchar*>(ptr), width, height, static_cast<int>(stride),
                       QImage::Format_ARGB32_Premultiplied);
        Stats s = runTimed(page, &surface, frames);
        frame->unmap();
        printStats("dma-buf +", width, height, frames, s);
        std::printf("  present=%s\n", mode);
        return 0;
    }

    QImage surface(width, height, QImage::Format_ARGB32_Premultiplied);
    Stats s = runTimed(page, &surface, frames);
    printStats("QImage +", width, height, frames, s);
    std::printf("  present=%s\n", mode);
    return 0;
}
