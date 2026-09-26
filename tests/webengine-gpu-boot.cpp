// #84: WebEngine boots and paints one frame under explicit Chromium GPU flags.
//
// qtwebkit-compat's beforeApplication only injects --disable-gpu when
// QTWEBENGINE_CHROMIUM_FLAGS is empty. This binary requires the flags in the
// environment (ctest sets them) and proves load + offscreen paint.
//
// Mutation: change kMinRed → must FAIL on a red page.

#include "qtwebkit_compat.h"

#include <QApplication>
#include <QColor>
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

static const int kMinRed = 100;
static const int kMaxRed = 240; // reject blank white (255) and miss the fill


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

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    const QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    if (flags.isEmpty() || flags.contains("--disable-gpu")) {
        std::fprintf(stderr,
                     "webengine-gpu-boot: refuse --disable-gpu / empty flags "
                     "(got [%s]); this test is the GPU path\n",
                     flags.constData());
        return 2;
    }
    std::printf("flags=[%s]\n", flags.constData());

    QApplication app(argc, argv);
    QWebPage page;
    page.setViewportSize(QSize(400, 300));
    bool loaded = false;
    QObject::connect(&page, &QWebPage::loadFinished, &page, [&](bool) { loaded = true; });
    page.mainFrame()->setHtml(
        QStringLiteral("<html><body style='margin:0;background:#cc3311'></body></html>"));
    if (!waitFor([&] { return loaded; }, 20000)) {
        std::printf("webengine-gpu-boot: FAIL load\n");
        return 1;
    }

    QImage img(400, 300, QImage::Format_ARGB32_Premultiplied);
    QColor c;
    if (!waitFor([&] {
            img.fill(Qt::transparent);
            {
                QPainter p(&img);
                page.mainFrame()->render(&p, QWebFrame::ContentsLayer, QRect(0, 0, 400, 300));
            }
            c = img.pixelColor(200, 150);
            return c.red() >= kMinRed && c.red() <= kMaxRed;
        }, 15000)) {
        std::printf("pixel a=%d r=%d g=%d b=%d\n", c.alpha(), c.red(), c.green(), c.blue());
        std::printf("webengine-gpu-boot: FAIL paint (need %d <= red <= %d)\n",
                    kMinRed, kMaxRed);
        return 1;
    }
    std::printf("pixel a=%d r=%d g=%d b=%d\n", c.alpha(), c.red(), c.green(), c.blue());
    std::printf("webengine-gpu-boot: ok\n");
    return 0;
}
