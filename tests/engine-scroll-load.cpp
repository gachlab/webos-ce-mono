// Under-load scroll with image-diff proof the page moved (#79 / #68).
//
// Same tall non-periodic page as the #81 WPE↔Qt harness, but A/B is the
// *present path* on QtWebEngine (qtwebkit-compat), not the engine:
//
//   QT_QPA_PLATFORM=offscreen ./engine-scroll-load          # all modes
//   QT_QPA_PLATFORM=offscreen ./engine-scroll-load grab
//   QT_QPA_PLATFORM=offscreen ./engine-scroll-load direct
//   QT_QPA_PLATFORM=offscreen ./engine-scroll-load dmabuf
//   QT_QPA_PLATFORM=offscreen ./engine-scroll-load dmabuf-gl
//
// Modes flip WEBOS_GRAB_PRESENT and optionally paint into a GBM dma-buf.
// dmabuf-gl: staging QImage → GlRenderTarget (EGL FBO export) → Host OES.
// Matches RemoteWindowDataDmaBuf. Present includes upload+import cost.

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
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include <EGL/egl.h>
#include <EGL/eglext.h>

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

enum class PresentMode { Grab, Direct, Dmabuf, DmabufGl };

static const char* modeName(PresentMode m)
{
    switch (m) {
    case PresentMode::Grab: return "grab";
    case PresentMode::Direct: return "direct";
    case PresentMode::Dmabuf: return "dmabuf";
    case PresentMode::DmabufGl: return "dmabuf-gl";
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
    std::unique_ptr<dmabuf_window::GlImporter> glImporter;
    std::unique_ptr<dmabuf_window::GlRenderTarget> glTarget;
    EGLDisplay surfDpy = EGL_NO_DISPLAY;
    EGLContext surfCtx = EGL_NO_CONTEXT;
    QImage dmaSurface;
    uint32_t dmaStride = 0;
    void* dmaPtr = nullptr;
    dmabuf_window::Export dmaExport{};

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
        if (!dmaFrame->exportDesc(&dmaExport)) {
            std::printf("%s: FAIL exportDesc\n", modeName(mode));
            return out;
        }
    } else if (mode == PresentMode::DmabufGl) {
        // Match RemoteWindowDataDmaBuf: staging QImage → GlRenderTarget upload →
        // dma-buf. (QPainter→FBO fights QtWebEngine's RHI on xcb.)
        auto getDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (!getDisplay) {
            std::printf("%s: FAIL no eglGetPlatformDisplayEXT\n", modeName(mode));
            return out;
        }
        EGLDisplay dpy = getDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY,
                                    nullptr);
        if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, nullptr, nullptr)
            || !eglBindAPI(EGL_OPENGL_ES_API)) {
            std::printf("%s: FAIL surfaceless EGL\n", modeName(mode));
            return out;
        }
        EGLint n = 0;
        eglGetConfigs(dpy, nullptr, 0, &n);
        std::vector<EGLConfig> configs(static_cast<size_t>(n));
        eglGetConfigs(dpy, configs.data(), n, &n);
        EGLConfig config = configs[0];
        static const EGLint attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, attrs);
        if (ctx == EGL_NO_CONTEXT
            || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
            std::printf("%s: FAIL EGL context\n", modeName(mode));
            return out;
        }
        surfDpy = dpy;
        surfCtx = ctx;
        glTarget = dmabuf_window::GlRenderTarget::create(static_cast<uint32_t>(width),
                                                         static_cast<uint32_t>(height));
        if (!glTarget) {
            std::printf("%s: FAIL GlRenderTarget\n", modeName(mode));
            return out;
        }
        dmaExport = glTarget->exportDesc();
        dmaExport.fd = ::dup(dmaExport.fd);
        if (dmaExport.fd < 0) {
            std::printf("%s: FAIL dup export fd\n", modeName(mode));
            return out;
        }
        glImporter = dmabuf_window::GlImporter::create();
        if (!glImporter) {
            std::printf("%s: FAIL GlImporter\n", modeName(mode));
            ::close(dmaExport.fd);
            dmaExport.fd = -1;
            return out;
        }
        eglMakeCurrent(surfDpy, EGL_NO_SURFACE, EGL_NO_SURFACE, surfCtx);
    }

    auto paintOnce = [&] {
        if (mode == PresentMode::DmabufGl) {
            eglMakeCurrent(surfDpy, EGL_NO_SURFACE, EGL_NO_SURFACE, surfCtx);
            QImage tmp(width, height, QImage::Format_ARGB32_Premultiplied);
            tmp.fill(Qt::transparent);
            {
                QPainter ctxt(&tmp);
                page.mainFrame()->render(&ctxt, QWebFrame::ContentsLayer,
                                         QRect(0, 0, width, height));
            }
            glTarget->uploadArgb32(reinterpret_cast<const uint32_t*>(tmp.constBits()),
                                   static_cast<uint32_t>(tmp.bytesPerLine() / 4));
            return;
        }
        surface->fill(Qt::transparent);
        QPainter ctxt(surface);
        page.mainFrame()->render(&ctxt, QWebFrame::ContentsLayer, QRect(0, 0, width, height));
    };

    auto glImportOnly = [&]() -> bool {
        return mode == PresentMode::DmabufGl && glImporter
            && glImporter->importFrame(dmaExport);
    };

    auto glSampleCenter = [&](uint32_t* argb) -> bool {
        return glImporter && glImporter->sampleImportedPixel(width / 2, height / 2, argb);
    };

    auto glFullImage = [&](QImage* composed) -> bool {
        std::vector<uint32_t> pixels;
        if (!glImporter->copyToArgb32(dmaExport, &pixels))
            return false;
        *composed = QImage(reinterpret_cast<const uchar*>(pixels.data()), width, height,
                           width * 4, QImage::Format_ARGB32_Premultiplied).copy();
        return true;
    };

    if (!waitFor([&] {
            paintOnce();
            if (mode == PresentMode::DmabufGl) {
                if (!glImportOnly())
                    return false;
                uint32_t px = 0;
                if (!glSampleCenter(&px))
                    return false;
                const int r = (px >> 16) & 0xff;
                const int g = (px >> 8) & 0xff;
                const int b = px & 0xff;
                const int a = (px >> 24) & 0xff;
                return a > 0 && (r > 10 || g > 10 || b > 10);
            }
            const QColor c = surface->pixelColor(width / 2, height / 2);
            return c.alpha() > 0 && (c.red() > 10 || c.green() > 10 || c.blue() > 10);
        }, 15000)) {
        std::printf("%s: FAIL first paint\n", modeName(mode));
        if (dmaExport.fd >= 0)
            ::close(dmaExport.fd);
        return out;
    }

    QImage before;
    uint32_t beforeSample = 0;
    if (mode == PresentMode::DmabufGl) {
        if (!glFullImage(&before) || !glSampleCenter(&beforeSample)) {
            std::printf("%s: FAIL first GL compose\n", modeName(mode));
            ::close(dmaExport.fd);
            return out;
        }
    } else {
        before = surface->copy();
    }

    std::vector<double> presentMs;
    presentMs.reserve(static_cast<size_t>(steps));
    QImage previous = before;
    uint32_t previousSample = beforeSample;
    QElapsedTimer wall;
    wall.start();
    for (int i = 0; i < steps; ++i) {
        page.mainFrame()->evaluateJavaScript(
            QStringLiteral("window.scrollBy(0, %1)").arg(qAbs(deltaY)));

        if (!waitFor([&] {
                paintOnce();
                if (mode == PresentMode::DmabufGl) {
                    if (!glImportOnly())
                        return false;
                    uint32_t cur = 0;
                    if (!glSampleCenter(&cur))
                        return false;
                    return cur != previousSample;
                }
                QImage cur = surface->copy();
                return differingPixels(previous, cur) > (width * height) / 100;
            }, 10000)) {
            std::printf("%s: FAIL no paint change after scroll step %d\n", modeName(mode), i);
            if (dmaExport.fd >= 0)
                ::close(dmaExport.fd);
            return out;
        }

        QElapsedTimer present;
        present.start();
        paintOnce();
        if (mode == PresentMode::DmabufGl) {
            if (!glImportOnly()) {
                std::printf("%s: FAIL GL import step %d\n", modeName(mode), i);
                ::close(dmaExport.fd);
                return out;
            }
        } else {
            previous = surface->copy();
        }
        presentMs.push_back(present.nsecsElapsed() / 1e6);
        if (mode == PresentMode::DmabufGl) {
            uint32_t cur = 0;
            if (!glSampleCenter(&cur)) {
                std::printf("%s: FAIL GL sample step %d\n", modeName(mode), i);
                ::close(dmaExport.fd);
                return out;
            }
            previousSample = cur;
        }
    }
    out.scrollWallMs = wall.elapsed();
    out.medianPresentMs = medianMs(presentMs);
    out.peakRssKb = readVmHwmKb();

    if (mode == PresentMode::DmabufGl) {
        QImage after;
        if (!glFullImage(&after)) {
            std::printf("%s: FAIL final GL readback\n", modeName(mode));
            ::close(dmaExport.fd);
            return out;
        }
        out.diffPixels = differingPixels(before, after);
    } else {
        out.diffPixels = differingPixels(before, previous);
    }
    const int minDiff = (width * height) / 20;
    out.moved = out.diffPixels >= minDiff;

    if (dmaFrame && dmaPtr)
        dmaFrame->unmap();
    if (dmaExport.fd >= 0)
        ::close(dmaExport.fd);
    if (surfDpy != EGL_NO_DISPLAY && surfCtx != EGL_NO_CONTEXT) {
        eglMakeCurrent(surfDpy, EGL_NO_SURFACE, EGL_NO_SURFACE, surfCtx);
        glTarget.reset();
        eglMakeCurrent(surfDpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
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
        else if (std::strcmp(arg, "dmabuf-gl") == 0)
            modes.push_back(PresentMode::DmabufGl);
        else {
            std::printf("usage: %s [grab|direct|dmabuf|dmabuf-gl]\n", argv[0]);
            return 1;
        }
    } else {
        modes = { PresentMode::Grab, PresentMode::Direct, PresentMode::Dmabuf,
                  PresentMode::DmabufGl };
    }

    int failures = 0;
    for (PresentMode m : modes)
        failures += report(runMode(m, width, height, steps, deltaY));

    std::printf("%s\n", failures ? "FAIL" : "OK");
    return failures ? 1 : 0;
}
