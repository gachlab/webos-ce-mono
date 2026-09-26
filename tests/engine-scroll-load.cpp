// Under-load scroll with image-diff proof the page moved (#79 / #68).
//
// Same tall non-periodic page as the #81 WPE↔Qt harness, but A/B is the
// *present path* on QtWebEngine (qtwebkit-compat), not the engine:
//
//   QT_QPA_PLATFORM=offscreen ./engine-scroll-load          # all modes
//   QT_QPA_PLATFORM=offscreen ./engine-scroll-load grab
//   QT_QPA_PLATFORM=offscreen ./engine-scroll-load direct
//   QT_QPA_PLATFORM=offscreen ./engine-scroll-load dmabuf
//
// Modes flip WEBOS_GRAB_PRESENT and optionally paint into a GBM dma-buf.
// No movement → FAIL (ctest).

#include "dmabuf_window.h"

#include <QApplication>
#include <QColor>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QPainter>
#include <QTimer>
#include <QWebFrame>
#include <QWebPage>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

static const char kScrollPage[] = R"HTML(<!doctype html>
<html><head><style>
  html, body { margin: 0; }
  /* Non-periodic: a repeating stripe made scroll-by-N look identical. */
  #strip {
    width: 100%;
    height: 4000px;
    background: linear-gradient(to bottom, #ff00c8, #0033aa, #00ff88, #ffaa00, #ff00c8);
  }
</style></head>
<body><div id="strip"></div></body></html>
)HTML";

enum class PresentMode { Grab, Direct, Dmabuf };

static const char* modeName(PresentMode m)
{
    switch (m) {
    case PresentMode::Grab: return "grab";
    case PresentMode::Direct: return "direct";
    case PresentMode::Dmabuf: return "dmabuf";
    }
    return "?";
}

static bool waitFor(const std::function<bool()>& done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > timeoutMs)
            return false;
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return true;
}

static int differingPixels(const QImage& a, const QImage& b)
{
    if (a.size() != b.size() || a.isNull() || b.isNull())
        return -1;
    const QImage aa = a.convertToFormat(QImage::Format_ARGB32);
    const QImage bb = b.convertToFormat(QImage::Format_ARGB32);
    int diff = 0;
    for (int y = 0; y < aa.height(); ++y) {
        const auto* pa = reinterpret_cast<const QRgb*>(aa.constScanLine(y));
        const auto* pb = reinterpret_cast<const QRgb*>(bb.constScanLine(y));
        for (int x = 0; x < aa.width(); ++x) {
            if (pa[x] != pb[x])
                ++diff;
        }
    }
    return diff;
}

static double medianMs(std::vector<double> samples)
{
    if (samples.empty())
        return 0;
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

static long readVmHwmKb()
{
    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f)
        return 0;
    char line[256];
    long kb = 0;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "VmHWM:", 6) == 0) {
            std::sscanf(line + 6, "%ld", &kb);
            break;
        }
    }
    std::fclose(f);
    return kb;
}

struct RunResult {
    PresentMode mode = PresentMode::Direct;
    bool moved = false;
    int diffPixels = 0;
    double scrollWallMs = 0;
    double medianPresentMs = 0;
    long peakRssKb = 0;
};

static RunResult runMode(PresentMode mode, int width, int height, int steps, double deltaY)
{
    RunResult out;
    out.mode = mode;

    if (mode == PresentMode::Grab)
        qputenv("WEBOS_GRAB_PRESENT", "1");
    else
        qputenv("WEBOS_GRAB_PRESENT", "0");

    QWebPage page;
    page.setViewportSize(QSize(width, height));
    bool loaded = false;
    QObject::connect(&page, &QWebPage::loadFinished, &page, [&](bool) { loaded = true; });
    page.mainFrame()->setHtml(QString::fromUtf8(kScrollPage));
    if (!waitFor([&] { return loaded; }, 30000)) {
        std::printf("%s: FAIL load\n", modeName(mode));
        return out;
    }

    QImage qimageSurface(width, height, QImage::Format_ARGB32_Premultiplied);
    std::unique_ptr<dmabuf_window::Frame> dmaFrame;
    QImage dmaSurface;
    uint32_t dmaStride = 0;
    void* dmaPtr = nullptr;

    QImage* surface = &qimageSurface;
    if (mode == PresentMode::Dmabuf) {
        if (!dmabuf_window::available()) {
            std::printf("%s: FAIL no GBM\n", modeName(mode));
            return out;
        }
        auto device = dmabuf_window::Device::openDefault();
        dmaFrame = dmabuf_window::Frame::create(device, static_cast<uint32_t>(width),
                                                static_cast<uint32_t>(height));
        if (!dmaFrame) {
            std::printf("%s: FAIL Frame::create\n", modeName(mode));
            return out;
        }
        dmaPtr = dmaFrame->mapWrite(&dmaStride);
        if (!dmaPtr) {
            std::printf("%s: FAIL mapWrite\n", modeName(mode));
            return out;
        }
        dmaSurface = QImage(static_cast<uchar*>(dmaPtr), width, height,
                            static_cast<int>(dmaStride), QImage::Format_ARGB32_Premultiplied);
        surface = &dmaSurface;
    }

    auto paintOnce = [&] {
        surface->fill(Qt::transparent);
        QPainter ctxt(surface);
        page.mainFrame()->render(&ctxt, QWebFrame::ContentsLayer, QRect(0, 0, width, height));
    };

    if (!waitFor([&] {
            paintOnce();
            const QColor c = surface->pixelColor(width / 2, height / 2);
            return c.alpha() > 0 && (c.red() > 10 || c.green() > 10 || c.blue() > 10);
        }, 15000)) {
        std::printf("%s: FAIL first paint\n", modeName(mode));
        return out;
    }
    const QImage before = surface->copy();

    std::vector<double> presentMs;
    presentMs.reserve(static_cast<size_t>(steps));
    QImage previous = surface->copy();
    QElapsedTimer wall;
    wall.start();
    for (int i = 0; i < steps; ++i) {
        // Offscreen QtWebEngine does not reliably apply synthetic QWheelEvent
        // (measured on #81). Drive scroll via sync JS; bar is image-diff + present.
        page.mainFrame()->evaluateJavaScript(
            QStringLiteral("window.scrollBy(0, %1)").arg(qAbs(deltaY)));

        if (!waitFor([&] {
                paintOnce();
                return differingPixels(previous, *surface) > (width * height) / 100;
            }, 10000)) {
            std::printf("%s: FAIL no paint change after scroll step %d\n", modeName(mode), i);
            return out;
        }

        QElapsedTimer present;
        present.start();
        paintOnce();
        presentMs.push_back(present.nsecsElapsed() / 1e6);
        previous = surface->copy();
    }
    out.scrollWallMs = wall.elapsed();
    out.medianPresentMs = medianMs(presentMs);
    out.peakRssKb = readVmHwmKb();

    out.diffPixels = differingPixels(before, *surface);
    const int minDiff = (width * height) / 20;
    out.moved = out.diffPixels >= minDiff;

    if (dmaFrame && dmaPtr)
        dmaFrame->unmap();
    return out;
}

static int report(const RunResult& r)
{
    const bool ok = r.moved;
    std::printf("%-8s : %-4s diff_pixels=%d scroll_wall_ms=%.1f median_present_ms=%.3f VmHWM_kb=%ld\n",
                modeName(r.mode), ok ? "yes" : "NO", r.diffPixels, r.scrollWallMs,
                r.medianPresentMs, r.peakRssKb);
    return ok ? 0 : 1;
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);

    const int width = 400;
    const int height = 600;
    const int steps = 20;
    const double deltaY = -80.0;

    std::vector<PresentMode> modes;
    if (argc > 1) {
        const char* arg = argv[1];
        if (std::strcmp(arg, "grab") == 0)
            modes.push_back(PresentMode::Grab);
        else if (std::strcmp(arg, "direct") == 0)
            modes.push_back(PresentMode::Direct);
        else if (std::strcmp(arg, "dmabuf") == 0)
            modes.push_back(PresentMode::Dmabuf);
        else {
            std::printf("usage: %s [grab|direct|dmabuf]\n", argv[0]);
            return 1;
        }
    } else {
        modes = { PresentMode::Grab, PresentMode::Direct, PresentMode::Dmabuf };
    }

    int failures = 0;
    for (PresentMode m : modes)
        failures += report(runMode(m, width, height, steps, deltaY));

    std::printf("%s\n", failures ? "FAIL" : "OK");
    return failures ? 1 : 0;
}
