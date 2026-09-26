// Contract: GPU-clear a GlRenderTarget FBO (exact readback) and export dma-buf
// so GlImporter can sample the same pixels (#79 GPU paint destination).
// Mutation: change kExpectedArgb (not kFillArgb) → must FAIL.

#include "dmabuf_window.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <cstdio>
#include <unistd.h>
#include <vector>

static const uint32_t kFillArgb = 0xFFCC3311u;
static const uint32_t kExpectedArgb = 0xFFCC3311u;

static bool makeSurfacelessCurrent(EGLDisplay* outDpy, EGLContext* outCtx)
{
    auto getDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (!getDisplay)
        return false;
    EGLDisplay dpy = getDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, nullptr, nullptr))
        return false;
    if (!eglBindAPI(EGL_OPENGL_ES_API))
        return false;
    EGLint n = 0;
    if (!eglGetConfigs(dpy, nullptr, 0, &n) || n < 1)
        return false;
    std::vector<EGLConfig> configs(static_cast<size_t>(n));
    eglGetConfigs(dpy, configs.data(), n, &n);
    EGLConfig config = configs[0];
    for (EGLint i = 0; i < n; ++i) {
        EGLint renderable = 0;
        eglGetConfigAttrib(dpy, configs[static_cast<size_t>(i)], EGL_RENDERABLE_TYPE, &renderable);
        if (renderable & EGL_OPENGL_ES2_BIT) {
            config = configs[static_cast<size_t>(i)];
            break;
        }
    }
    static const EGLint attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, attrs);
    if (ctx == EGL_NO_CONTEXT)
        return false;
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
        return false;
    *outDpy = dpy;
    *outCtx = ctx;
    return true;
}

int main()
{
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLContext ctx = EGL_NO_CONTEXT;
    if (!makeSurfacelessCurrent(&dpy, &ctx)) {
        std::fprintf(stderr, "dmabuf-gl-paint: no EGL context\n");
        return 1;
    }

    auto target = dmabuf_window::GlRenderTarget::create(64, 48);
    if (!target || !target->clearArgb(kFillArgb)) {
        std::fprintf(stderr, "dmabuf-gl-paint: create/clear failed\n");
        return 1;
    }

    // Exact readback from the paint FBO (mutation target).
    if (!target->begin()) {
        std::fprintf(stderr, "dmabuf-gl-paint: begin for readback failed\n");
        return 1;
    }
    GLubyte px[4] = {};
    glReadPixels(32, 24, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    target->end();
    const uint32_t got = (uint32_t(px[3]) << 24) | (uint32_t(px[0]) << 16)
                         | (uint32_t(px[1]) << 8) | uint32_t(px[2]);
    if (got != kExpectedArgb) {
        std::fprintf(stderr, "dmabuf-gl-paint: pixel mismatch got=0x%08x expected=0x%08x\n",
                     got, kExpectedArgb);
        return 1;
    }

    auto gl = dmabuf_window::GlImporter::create();
    if (!gl) {
        std::fprintf(stderr, "dmabuf-gl-paint: GlImporter failed\n");
        return 1;
    }
    uint32_t imported = 0;
    if (!gl->samplePixel(target->exportDesc(), 32, 24, &imported)) {
        std::fprintf(stderr, "dmabuf-gl-paint: export/import sample failed\n");
        return 1;
    }
    // Export path may quantize; require same alpha + coarse RGB.
    if ((imported >> 24) != 0xffu || ((imported >> 16) & 0xf0) != 0xc0
        || ((imported >> 8) & 0xf0) != 0x30) {
        std::fprintf(stderr, "dmabuf-gl-paint: imported pixel off got=0x%08x\n", imported);
        return 1;
    }

    std::printf("dmabuf-gl-paint: ok pixel=0x%08x imported=0x%08x\n", got, imported);
    return 0;
}
