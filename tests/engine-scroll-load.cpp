// Under-load scroll with image-diff proof the page moved (#81 / #68).
//
// Runs the same tall striped page on WPE (headless → ARGB32_Premultiplied blit)
// and on QtWebEngine through qtwebkit-compat (the real WebAppMgr paint path).
// Each backend must change enough pixels after wheel/scroll input; wall time and
// peak RSS are printed for the go/no-go table.
//
// Not a silent benchmark: no movement → FAIL.

#include "wpe_webcontent.h"

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
  /* Non-periodic: a repeating 80px stripe made scroll-by-80 look identical. */
  #strip {
    width: 100%;
    height: 4000px;
    background: linear-gradient(to bottom, #ff00c8, #0033aa, #00ff88, #ffaa00, #ff00c8);
  }
</style></head>
<body><div id="strip"></div></body></html>
)HTML";

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

struct RunResult {
    const char* engine = nullptr;
    bool moved = false;
    int diffPixels = 0;
    double scrollWallMs = 0;
    double medianBlitMs = 0;
    long peakRssKb = 0;
};

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

static RunResult runWpe(int width, int height, int steps, double deltaY)
{
    RunResult out;
    out.engine = "wpe";

    wpe_webcontent::HeadlessView view(width, height);
    view.loadHtml(QString::fromUtf8(kScrollPage));
    if (!view.waitUntilLoaded(30000) || !view.waitUntil([&] { return view.hasFrame(); }, 10000)) {
        std::printf("wpe: FAIL load/frame\n");
        return out;
    }

    // Settle: one scroll that actually moves (WPE: negative deltaY increases
    // scrollY — measured). Clear and wait for the post-scroll present so the
    // before-shot is a known scrolled frame? No: we want before at top.
    // Just ensure we have a stable top frame.
    if (!view.hasFrame()) {
        std::printf("wpe: FAIL no before-frame\n");
        return out;
    }
    const QImage before = view.frame().copy();

    std::vector<double> blitMs;
    blitMs.reserve(static_cast<size_t>(steps));
    QImage surface(width, height, QImage::Format_ARGB32_Premultiplied);

    QElapsedTimer wall;
    wall.start();
    for (int i = 0; i < steps; ++i) {
        view.clearFrame();
        // Negative deltaY scrolls content down (scrollY increases). Positive
        // from the top is a no-op — measured on WPE 2.54 headless.
        view.scroll(width / 2.0, height / 2.0, 0, deltaY);
        if (!view.waitUntil([&] { return view.hasFrame(); }, 5000)) {
            std::printf("wpe: FAIL no frame after scroll step %d\n", i);
            return out;
        }

        QElapsedTimer blit;
        blit.start();
        surface.fill(Qt::transparent);
        {
            QPainter ctxt(&surface);
            ctxt.drawImage(0, 0, view.frame());
        }
        blitMs.push_back(blit.nsecsElapsed() / 1e6);
    }
    out.scrollWallMs = wall.elapsed();
    out.medianBlitMs = medianMs(blitMs);
    out.peakRssKb = readVmHwmKb();

    const QImage after = view.frame();
    out.diffPixels = differingPixels(before, after);
    // Stripes are 40px; several steps of 80px should recolour a large fraction.
    const int minDiff = (width * height) / 20; // 5% of pixels
    out.moved = out.diffPixels >= minDiff;
    return out;
}

static RunResult runQtWebEngine(int width, int height, int steps, double deltaY)
{
    RunResult out;
    out.engine = "qtwebengine";

    QWebPage page;
    page.setViewportSize(QSize(width, height));
    bool loaded = false;
    QObject::connect(&page, &QWebPage::loadFinished, &page, [&](bool) { loaded = true; });
    page.mainFrame()->setHtml(QString::fromUtf8(kScrollPage));
    if (!waitFor([&] { return loaded; }, 30000)) {
        std::printf("qtwebengine: FAIL load\n");
        return out;
    }
    // First paint.
    QImage surface(width, height, QImage::Format_ARGB32_Premultiplied);
    auto paintOnce = [&] {
        surface.fill(Qt::transparent);
        QPainter ctxt(&surface);
        page.mainFrame()->render(&ctxt, QWebFrame::ContentsLayer, QRect(0, 0, width, height));
    };
    if (!waitFor([&] {
            paintOnce();
            const QColor c = surface.pixelColor(width / 2, height / 2);
            return c.alpha() > 0 && (c.red() > 10 || c.green() > 10 || c.blue() > 10);
        }, 15000)) {
        std::printf("qtwebengine: FAIL first paint (center=%s)\n",
                    qPrintable(surface.pixelColor(width / 2, height / 2).name()));
        return out;
    }
    const QImage before = surface.copy();

    std::vector<double> blitMs;
    blitMs.reserve(static_cast<size_t>(steps));
    QImage previous = surface.copy();
    QElapsedTimer wall;
    wall.start();
    for (int i = 0; i < steps; ++i) {
        // Offscreen QtWebEngine does not reliably apply synthetic QWheelEvent
        // within a few seconds (measured: 19×5s timeouts, scroll applied late).
        // Drive scroll through the sync JS bridge WebAppMgr already uses; the
        // bar is still image-diff proof + blit cost under load. WPE above uses
        // real scroll events.
        page.mainFrame()->evaluateJavaScript(
            QStringLiteral("window.scrollBy(0, %1)").arg(qAbs(deltaY)));

        if (!waitFor([&] {
                paintOnce();
                return differingPixels(previous, surface) > (width * height) / 100;
            }, 10000)) {
            std::printf("qtwebengine: FAIL no paint change after scroll step %d\n", i);
            return out;
        }

        QElapsedTimer blit;
        blit.start();
        paintOnce();
        blitMs.push_back(blit.nsecsElapsed() / 1e6);
        previous = surface.copy();
    }
    out.scrollWallMs = wall.elapsed();
    out.medianBlitMs = medianMs(blitMs);
    out.peakRssKb = readVmHwmKb();

    out.diffPixels = differingPixels(before, surface);
    const int minDiff = (width * height) / 20;
    out.moved = out.diffPixels >= minDiff;
    return out;
}

static int report(const RunResult& r)
{
    const bool ok = r.moved;
    std::printf("%-12s : %-4s diff_pixels=%d scroll_wall_ms=%.1f median_blit_ms=%.3f VmHWM_kb=%ld\n",
                r.engine, ok ? "yes" : "NO", r.diffPixels, r.scrollWallMs, r.medianBlitMs,
                r.peakRssKb);
    return ok ? 0 : 1;
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);

    const int width = 400;
    const int height = 600;
    const int steps = 20;

    int failures = 0;
    // WPE first so its VmHWM is not inflated by Chromium still resident.
    // deltaY < 0 scrolls down from the top on both backends (WPE measured;
    // Qt pixelDelta negative likewise).
    const double deltaY = -80.0;
    failures += report(runWpe(width, height, steps, deltaY));
    failures += report(runQtWebEngine(width, height, steps, deltaY));

    std::printf("%s\n", failures ? "FAIL" : "OK");
    return failures ? 1 : 0;
}
