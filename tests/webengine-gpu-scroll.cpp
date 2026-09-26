// #84: Chromium GPU flags must keep updating frames (scroll), not only the
// first paint. webengine-gpu-boot covers static load+paint; this covers the
// WebAppMgr-shaped failure mode (context loss / frozen present).
//
// Product default is ANGLE+gl (hardware under Wayland). Under QT_QPA_PLATFORM=
// offscreen, native EGL freezes scroll; ANGLE SwiftShader still updates — so
// ctest uses that combo. Seat0 / Wayland smoke validates ANGLE+gl.
//
// Mutation: change kMinDiffFraction → must FAIL on a moving gradient page.

#include "qtwebkit_compat.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QPainter>
#include <QTimer>
#include <QWebFrame>
#include <QWebPage>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

static const char kScrollPage[] = R"HTML(<!doctype html>
<html><head><style>
  html, body { margin: 0; }
  #strip {
    width: 100%;
    height: 4000px;
    background: linear-gradient(to bottom, #ff00c8, #0033aa, #00ff88, #ffaa00, #ff00c8);
  }
</style></head>
<body><div id="strip"></div></body></html>
)HTML";

// At least 5% of pixels must differ after scrolling the gradient.
static const double kMinDiffFraction = 0.05;

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

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    const QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    if (flags.isEmpty() || flags.contains("--disable-gpu")) {
        std::fprintf(stderr,
                     "webengine-gpu-scroll: refuse --disable-gpu / empty flags "
                     "(got [%s]); this test is the GPU path\n",
                     flags.constData());
        return 2;
    }
    std::printf("flags=[%s] qpa=[%s]\n", flags.constData(),
                qgetenv("QT_QPA_PLATFORM").constData());

    QApplication app(argc, argv);

    const int width = 400;
    const int height = 600;
    QWebPage page;
    page.setViewportSize(QSize(width, height));
    bool loaded = false;
    QObject::connect(&page, &QWebPage::loadFinished, &page, [&](bool) { loaded = true; });
    page.mainFrame()->setHtml(QString::fromUtf8(kScrollPage));
    if (!waitFor([&] { return loaded; }, 20000)) {
        std::printf("webengine-gpu-scroll: FAIL load\n");
        return 1;
    }

    QImage surface(width, height, QImage::Format_ARGB32_Premultiplied);
    auto paint = [&] {
        surface.fill(Qt::transparent);
        QPainter p(&surface);
        page.mainFrame()->render(&p, QWebFrame::ContentsLayer, QRect(0, 0, width, height));
    };

    if (!waitFor([&] {
            paint();
            const QColor c = surface.pixelColor(width / 2, height / 2);
            return c.alpha() > 0 && (c.red() > 10 || c.green() > 10 || c.blue() > 10);
        }, 15000)) {
        std::printf("webengine-gpu-scroll: FAIL first paint\n");
        return 1;
    }

    const QImage before = surface.copy();
    page.mainFrame()->evaluateJavaScript(QStringLiteral("window.scrollBy(0, 400)"));

    QImage after;
    if (!waitFor([&] {
            paint();
            after = surface.copy();
            return differingPixels(before, after) > int(width * height * kMinDiffFraction);
        }, 15000)) {
        const int diff = differingPixels(before, after.isNull() ? surface : after);
        std::printf("diff_pixels=%d (need > %d)\n", diff,
                     int(width * height * kMinDiffFraction));
        std::printf("webengine-gpu-scroll: FAIL scroll did not change paint "
                    "(GPU path frozen / context loss)\n");
        return 1;
    }

    std::printf("diff_pixels=%d\n", differingPixels(before, after));
    std::printf("webengine-gpu-scroll: ok\n");
    return 0;
}
