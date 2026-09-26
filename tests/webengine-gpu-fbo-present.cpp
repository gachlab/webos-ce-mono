// #84 spike: redirect QWebEngine's QQuickWindow into our GlRenderTarget texture
// (dma-buf export) via QQuickRenderTarget::fromOpenGLTexture — no staging QImage.
//
//   QT_QPA_PLATFORM=wayland QTWEBENGINE_CHROMIUM_FLAGS='--use-gl=angle --use-angle=gl --enable-gpu-rasterization' \
//     ./webengine-gpu-fbo-present
//
// Exit 0 = centre sample changes after scrollBy.

#include "dmabuf_window.h"
#include "qtwebkit_compat.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QQuickRenderTarget>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QTimer>
#include <QWebEngineView>
#include <QWebFrame>
#include <QWebPage>

#include <QCoreApplication>
#include <cstdio>
#include <functional>

static const char kScrollPage[] = R"HTML(<!doctype html>
<html><head><style>
  html, body { margin: 0; }
  #strip {
    width: 100%; height: 4000px;
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
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return true;
}

static QQuickWidget* quickSurface(QWebPage* page)
{
    QWebEngineView* view = page->engineView();
    return view ? qobject_cast<QQuickWidget*>(view->focusProxy()) : nullptr;
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (qgetenv("QT_QPA_PLATFORM") == "offscreen"
        || (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
            && qEnvironmentVariableIsEmpty("DISPLAY"))) {
        std::printf("webengine-gpu-fbo-present: SKIP no interactive display\n");
        return 77;
    }
    const QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    if (flags.isEmpty() || flags.contains("--disable-gpu")) {
        std::fprintf(stderr, "refuse --disable-gpu / empty flags\n");
        return 2;
    }
    std::printf("flags=[%s] qpa=[%s]\n", flags.constData(),
                qgetenv("QT_QPA_PLATFORM").constData());

    QApplication app(argc, argv);

    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGLES);
    fmt.setVersion(2, 0);
    fmt.setAlphaBufferSize(8);

    QOffscreenSurface surface;
    surface.setFormat(fmt);
    surface.create();
    QOpenGLContext gl;
    gl.setFormat(fmt);
    if (QOpenGLContext* share = QOpenGLContext::globalShareContext())
        gl.setShareContext(share);
    if (!surface.isValid() || !gl.create() || !gl.makeCurrent(&surface)) {
        std::printf("FAIL GL context (share=%p)\n",
                    static_cast<void*>(QOpenGLContext::globalShareContext()));
        return 1;
    }
    std::printf("share=%p\n", static_cast<void*>(QOpenGLContext::globalShareContext()));

    const int width = 400;
    const int height = 600;
    auto target = dmabuf_window::GlRenderTarget::create(static_cast<uint32_t>(width),
                                                        static_cast<uint32_t>(height));
    if (!target) {
        std::printf("FAIL GlRenderTarget\n");
        return 1;
    }
    auto importer = dmabuf_window::GlImporter::createAttached();
    if (!importer) {
        std::printf("FAIL GlImporter\n");
        return 1;
    }

    QWebPage page;
    page.setViewportSize(QSize(width, height));
    bool loaded = false;
    QObject::connect(&page, &QWebPage::loadFinished, &page, [&](bool) { loaded = true; });
    page.mainFrame()->setHtml(QString::fromUtf8(kScrollPage));
    if (!waitFor([&] { return loaded; }, 20000)) {
        std::printf("FAIL load\n");
        return 1;
    }

    // Wait until QtWebEngine has created its QQuickWidget surface, then redirect
    // its render target into our dma-buf-backed texture.
    QQuickWindow* qwin = nullptr;
    if (!waitFor([&] {
            QQuickWidget* qw = quickSurface(&page);
            if (!qw || !qw->quickWindow())
                return false;
            qwin = qw->quickWindow();
            return true;
        }, 10000)) {
        std::printf("FAIL no QQuickWindow\n");
        return 1;
    }

    if (!gl.makeCurrent(&surface)) {
        std::printf("FAIL makeCurrent before setRenderTarget\n");
        return 1;
    }
    QQuickRenderTarget rt = QQuickRenderTarget::fromOpenGLTexture(
        target->colorTexture(), QSize(width, height));
    qwin->setRenderTarget(rt);
    std::printf("setRenderTarget tex=%u\n", target->colorTexture());

    // Kick a frame.
    qwin->update();
    QCoreApplication::processEvents();

    uint32_t before = 0;
    if (!waitFor([&] {
            qwin->update();
            QCoreApplication::processEvents();
            if (!gl.makeCurrent(&surface))
                return false;
            dmabuf_window::Export desc = target->exportDesc();
            if (!importer->importFrame(desc))
                return false;
            uint32_t px = 0;
            if (!importer->sampleImportedPixel(width / 2, height / 2, &px))
                return false;
            before = px;
            const int a = (px >> 24) & 0xff;
            const int r = (px >> 16) & 0xff;
            const int g = (px >> 8) & 0xff;
            const int b = px & 0xff;
            return a > 0 && (r > 10 || g > 10 || b > 10);
        }, 15000)) {
        std::printf("FAIL first redirected paint (sample=0x%08x)\n", before);
        return 1;
    }
    std::printf("before=0x%08x\n", before);

    page.mainFrame()->evaluateJavaScript(QStringLiteral("window.scrollBy(0, 400)"));

    uint32_t after = before;
    if (!waitFor([&] {
            qwin->update();
            QCoreApplication::processEvents();
            if (!gl.makeCurrent(&surface))
                return false;
            dmabuf_window::Export desc = target->exportDesc();
            if (!importer->importFrame(desc))
                return false;
            if (!importer->sampleImportedPixel(width / 2, height / 2, &after))
                return false;
            return after != before;
        }, 15000)) {
        std::printf("after=0x%08x\n", after);
        std::printf("FAIL scroll did not change redirected FBO\n");
        return 1;
    }

    std::printf("after=0x%08x\n", after);
    std::printf("webengine-gpu-fbo-present: ok\n");
    return 0;
}
