/* @@@LICENSE
 *
 * Copyright (c) 2026 webOS CE modern build
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * LICENSE@@@ */

#include "dmabuf_window.h"

#include <cstring>
#include <vector>

#include <drm_fourcc.h>
#include <gbm.h>

#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

namespace dmabuf_window {
namespace {

PFNEGLGETPLATFORMDISPLAYEXTPROC getGetPlatformDisplay()
{
    static auto fn = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    return fn;
}

PFNEGLCREATEIMAGEKHRPROC getCreateImage()
{
    static auto fn = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
        eglGetProcAddress("eglCreateImageKHR"));
    return fn;
}

PFNEGLDESTROYIMAGEKHRPROC getDestroyImage()
{
    static auto fn = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
        eglGetProcAddress("eglDestroyImageKHR"));
    return fn;
}

PFNGLEGLIMAGETARGETTEXTURE2DOESPROC getImageTargetTexture()
{
    static auto fn = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    return fn;
}

EGLDisplay openSurfacelessDisplay()
{
    auto getDisplay = getGetPlatformDisplay();
    if (!getDisplay)
        return EGL_NO_DISPLAY;
    EGLDisplay dpy = getDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY,
                                nullptr);
    if (dpy == EGL_NO_DISPLAY)
        return EGL_NO_DISPLAY;
    if (!eglInitialize(dpy, nullptr, nullptr))
        return EGL_NO_DISPLAY;
    return dpy;
}

GLuint compileShader(GLenum type, const char* src)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

// --- CPU Importer (phase 1) -------------------------------------------------

std::unique_ptr<Importer> Importer::create()
{
    auto device = Device::openDefault();
    if (!device || !device->valid())
        return {};
    auto importer = std::unique_ptr<Importer>(new Importer);
    importer->m_device = std::move(device);
    return importer;
}

Importer::~Importer() = default;

bool Importer::samplePixel(const Export& desc, int x, int y, uint32_t* argb)
{
    if (!argb || !valid() || desc.fd < 0 || desc.width == 0 || desc.height == 0)
        return false;
    if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= desc.width
        || static_cast<uint32_t>(y) >= desc.height)
        return false;

    auto* gbm = static_cast<gbm_device*>(m_device->gbmDevice());
    gbm_import_fd_data importData{};
    importData.fd = desc.fd;
    importData.width = desc.width;
    importData.height = desc.height;
    importData.stride = desc.stride;
    importData.format = GBM_FORMAT_ARGB8888;

    gbm_bo* bo = gbm_bo_import(gbm, GBM_BO_IMPORT_FD, &importData,
                               GBM_BO_USE_LINEAR | GBM_BO_USE_RENDERING);
    if (!bo)
        return false;

    uint32_t mapStride = 0;
    void* mapData = nullptr;
    void* ptr = gbm_bo_map(bo, 0, 0, desc.width, desc.height, GBM_BO_TRANSFER_READ,
                           &mapStride, &mapData);
    bool ok = false;
    if (ptr) {
        auto* row = reinterpret_cast<const uint32_t*>(
            static_cast<const uint8_t*>(ptr) + static_cast<size_t>(y) * mapStride);
        *argb = row[x];
        ok = true;
        gbm_bo_unmap(bo, mapData);
    }
    gbm_bo_destroy(bo);
    return ok;
}

// --- GL Importer (EXTERNAL_OES) ---------------------------------------------

static bool hasExternalOes()
{
    const char* exts = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    return exts && std::strstr(exts, "GL_OES_EGL_image_external");
}

std::unique_ptr<GlImporter> GlImporter::create()
{
    EGLDisplay dpy = openSurfacelessDisplay();
    if (dpy == EGL_NO_DISPLAY)
        return {};

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        eglTerminate(dpy);
        return {};
    }

    EGLint numConfigs = 0;
    if (!eglGetConfigs(dpy, nullptr, 0, &numConfigs) || numConfigs < 1) {
        eglTerminate(dpy);
        return {};
    }
    std::vector<EGLConfig> configs(static_cast<size_t>(numConfigs));
    if (!eglGetConfigs(dpy, configs.data(), numConfigs, &numConfigs) || numConfigs < 1) {
        eglTerminate(dpy);
        return {};
    }
    EGLConfig config = nullptr;
    for (EGLint i = 0; i < numConfigs; ++i) {
        EGLint renderable = 0;
        eglGetConfigAttrib(dpy, configs[static_cast<size_t>(i)], EGL_RENDERABLE_TYPE,
                           &renderable);
        if (renderable & EGL_OPENGL_ES2_BIT) {
            config = configs[static_cast<size_t>(i)];
            break;
        }
    }
    if (!config)
        config = configs[0];

    static const EGLint ctxAttrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctxAttrs);
    if (ctx == EGL_NO_CONTEXT) {
        eglTerminate(dpy);
        return {};
    }
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        eglDestroyContext(dpy, ctx);
        eglTerminate(dpy);
        return {};
    }

    if (!hasExternalOes()) {
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(dpy, ctx);
        eglTerminate(dpy);
        return {};
    }

    auto importer = std::unique_ptr<GlImporter>(new GlImporter);
    importer->m_display = dpy;
    importer->m_context = ctx;
    importer->m_ownsContext = true;
    if (!importer->ensureExtProgram()) {
        importer.reset();
        return {};
    }
    return importer;
}

std::unique_ptr<GlImporter> GlImporter::createAttached()
{
    EGLDisplay dpy = eglGetCurrentDisplay();
    EGLContext ctx = eglGetCurrentContext();
    if (dpy == EGL_NO_DISPLAY || ctx == EGL_NO_CONTEXT)
        return {};
    if (!hasExternalOes())
        return {};

    auto importer = std::unique_ptr<GlImporter>(new GlImporter);
    importer->m_display = dpy;
    importer->m_context = ctx;
    importer->m_ownsContext = false;
    if (!importer->ensureExtProgram()) {
        importer.reset();
        return {};
    }
    return importer;
}

GlImporter::~GlImporter()
{
    auto* dpy = static_cast<EGLDisplay>(m_display);
    auto* ctx = static_cast<EGLContext>(m_context);
    if (dpy && ctx && m_ownsContext)
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
    else if (dpy && ctx && !m_ownsContext) {
        // Attached: only delete GL objects if this context is still current.
        if (eglGetCurrentContext() != ctx)
            return;
    }
    if (m_fbo)
        glDeleteFramebuffers(1, &m_fbo);
    if (m_colorTex)
        glDeleteTextures(1, &m_colorTex);
    if (m_extTex)
        glDeleteTextures(1, &m_extTex);
    if (m_extProgram)
        glDeleteProgram(m_extProgram);
    if (m_drawProgram)
        glDeleteProgram(m_drawProgram);
    if (m_ownsContext && dpy) {
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (ctx)
            eglDestroyContext(dpy, ctx);
        eglTerminate(dpy);
    }
    m_display = nullptr;
    m_context = nullptr;
}

bool GlImporter::makeCurrent() const
{
    auto* dpy = static_cast<EGLDisplay>(m_display);
    auto* ctx = static_cast<EGLContext>(m_context);
    if (!dpy || !ctx)
        return false;
    if (!m_ownsContext)
        return eglGetCurrentContext() == ctx;
    return eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
}

bool GlImporter::ensureExtProgram()
{
    static const char kVs[] =
        "attribute vec2 aPos;\n"
        "varying vec2 vUv;\n"
        "void main(){\n"
        "  vUv = aPos * 0.5 + 0.5;\n"
        "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "}\n";
    static const char kFs[] =
        "#extension GL_OES_EGL_image_external : require\n"
        "precision mediump float;\n"
        "uniform samplerExternalOES uTex;\n"
        "varying vec2 vUv;\n"
        "void main(){ gl_FragColor = texture2D(uTex, vUv); }\n";

    GLuint vs = compileShader(GL_VERTEX_SHADER, kVs);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFs);
    if (!vs || !fs) {
        if (vs)
            glDeleteShader(vs);
        if (fs)
            glDeleteShader(fs);
        return false;
    }
    m_extProgram = glCreateProgram();
    glAttachShader(m_extProgram, vs);
    glAttachShader(m_extProgram, fs);
    glBindAttribLocation(m_extProgram, 0, "aPos");
    glLinkProgram(m_extProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(m_extProgram, GL_LINK_STATUS, &linked);
    if (!linked) {
        glDeleteProgram(m_extProgram);
        m_extProgram = 0;
        return false;
    }
    glGenTextures(1, &m_extTex);
    return true;
}

bool GlImporter::ensureDrawProgram()
{
    if (m_drawProgram)
        return true;
    static const char kVs[] =
        "attribute vec2 aPos;\n"
        "attribute vec2 aUv;\n"
        "varying vec2 vUv;\n"
        "void main(){\n"
        "  vUv = aUv;\n"
        "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "}\n";
    static const char kFs[] =
        "precision mediump float;\n"
        "uniform sampler2D uTex;\n"
        "varying vec2 vUv;\n"
        "void main(){ gl_FragColor = texture2D(uTex, vUv); }\n";

    GLuint vs = compileShader(GL_VERTEX_SHADER, kVs);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFs);
    if (!vs || !fs) {
        if (vs)
            glDeleteShader(vs);
        if (fs)
            glDeleteShader(fs);
        return false;
    }
    m_drawProgram = glCreateProgram();
    glAttachShader(m_drawProgram, vs);
    glAttachShader(m_drawProgram, fs);
    glBindAttribLocation(m_drawProgram, 0, "aPos");
    glBindAttribLocation(m_drawProgram, 1, "aUv");
    glLinkProgram(m_drawProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(m_drawProgram, GL_LINK_STATUS, &linked);
    if (!linked) {
        glDeleteProgram(m_drawProgram);
        m_drawProgram = 0;
        return false;
    }
    return true;
}

bool GlImporter::blitToFbo(const Export& desc)
{
    if (!valid() || desc.fd < 0 || desc.width == 0 || desc.height == 0)
        return false;
    if (!makeCurrent())
        return false;

    auto createImage = getCreateImage();
    auto destroyImage = getDestroyImage();
    auto imageTarget = getImageTargetTexture();
    if (!createImage || !destroyImage || !imageTarget)
        return false;

    auto* dpy = static_cast<EGLDisplay>(m_display);

    EGLint attrs[20];
    int i = 0;
    attrs[i++] = EGL_WIDTH;
    attrs[i++] = static_cast<EGLint>(desc.width);
    attrs[i++] = EGL_HEIGHT;
    attrs[i++] = static_cast<EGLint>(desc.height);
    attrs[i++] = EGL_LINUX_DRM_FOURCC_EXT;
    attrs[i++] = static_cast<EGLint>(desc.fourcc ? desc.fourcc : DRM_FORMAT_ARGB8888);
    attrs[i++] = EGL_DMA_BUF_PLANE0_FD_EXT;
    attrs[i++] = desc.fd;
    attrs[i++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
    attrs[i++] = static_cast<EGLint>(desc.offset);
    attrs[i++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
    attrs[i++] = static_cast<EGLint>(desc.stride);
    if (desc.modifier != 0 && desc.modifier != DRM_FORMAT_MOD_INVALID) {
        attrs[i++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
        attrs[i++] = static_cast<EGLint>(desc.modifier & 0xffffffffu);
        attrs[i++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
        attrs[i++] = static_cast<EGLint>(desc.modifier >> 32);
    }
    attrs[i++] = EGL_NONE;

    EGLImageKHR image = createImage(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
                                    nullptr, attrs);
    if (image == EGL_NO_IMAGE_KHR)
        return false;

    GLint prevFbo = 0;
    GLint prevViewport[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    glBindTexture(GL_TEXTURE_EXTERNAL_OES, m_extTex);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    imageTarget(GL_TEXTURE_EXTERNAL_OES, image);

    if (m_fboW != desc.width || m_fboH != desc.height) {
        if (m_fbo)
            glDeleteFramebuffers(1, &m_fbo);
        if (m_colorTex)
            glDeleteTextures(1, &m_colorTex);
        m_fbo = 0;
        m_colorTex = 0;
        glGenTextures(1, &m_colorTex);
        glBindTexture(GL_TEXTURE_2D, m_colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(desc.width),
                     static_cast<GLsizei>(desc.height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glGenFramebuffers(1, &m_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               m_colorTex, 0);
        m_fboW = desc.width;
        m_fboH = desc.height;
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    }

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        destroyImage(dpy, image);
        glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        return false;
    }

    glViewport(0, 0, static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height));
    glUseProgram(m_extProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, m_extTex);
    glUniform1i(glGetUniformLocation(m_extProgram, "uTex"), 0);
    static const GLfloat verts[] = { -1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f };
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);

    destroyImage(dpy, image);
    // Restore Qt's FBO so drawColorTexture / beginNativePainting stay valid.
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    return true;
}

bool GlImporter::importFrame(const Export& desc)
{
    return blitToFbo(desc);
}

bool GlImporter::sampleImportedPixel(int x, int y, uint32_t* argb)
{
    if (!argb || !m_fbo || m_fboW == 0 || m_fboH == 0)
        return false;
    if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= m_fboW
        || static_cast<uint32_t>(y) >= m_fboH)
        return false;
    if (!makeCurrent())
        return false;

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    GLubyte px[4] = {0, 0, 0, 0};
    // FBO blit used vUv = aPos*0.5+0.5 with Y increasing up in clip space, so
    // row 0 in the texture is the bottom of the source. Match samplePixel.
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    *argb = (uint32_t(px[3]) << 24) | (uint32_t(px[0]) << 16)
            | (uint32_t(px[1]) << 8) | uint32_t(px[2]);
    return true;
}

bool GlImporter::drawColorTexture(int fbWidth, int fbHeight,
                                  int destX, int destY, int destW, int destH)
{
    if (!m_colorTex || fbWidth <= 0 || fbHeight <= 0 || destW <= 0 || destH <= 0)
        return false;
    if (!makeCurrent() || !ensureDrawProgram())
        return false;

    // Top-left dest → GL NDC (Y up).
    const float x0 = 2.f * float(destX) / float(fbWidth) - 1.f;
    const float x1 = 2.f * float(destX + destW) / float(fbWidth) - 1.f;
    const float y0 = 1.f - 2.f * float(destY + destH) / float(fbHeight);
    const float y1 = 1.f - 2.f * float(destY) / float(fbHeight);

    const GLfloat verts[] = {
        x0, y0, x1, y0, x0, y1, x1, y1,
    };
    // dma-buf / OES blit leave the card's top at V=0; flip V so top-left dest
    // samples the top of the page (seat0 globe was upside-down without this).
    const GLfloat uvs[] = {
        0.f, 1.f, 1.f, 1.f, 0.f, 0.f, 1.f, 0.f,
    };

    // Stay on Qt's current FBO (QOpenGLWidget / beginNativePainting). Binding
    // 0 drew into the wrong buffer and showed card-wide scanlines on seat0.
    GLint prevFbo = 0;
    GLint prevViewport[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    glViewport(0, 0, fbWidth, fbHeight);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_drawProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_colorTex);
    glUniform1i(glGetUniformLocation(m_drawProgram, "uTex"), 0);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uvs);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    return true;
}

bool GlImporter::samplePixel(const Export& desc, int x, int y, uint32_t* argb)
{
    if (!argb || x < 0 || y < 0
        || static_cast<uint32_t>(x) >= desc.width
        || static_cast<uint32_t>(y) >= desc.height)
        return false;
    if (!importFrame(desc))
        return false;
    return sampleImportedPixel(x, y, argb);
}

bool GlImporter::copyToArgb32(const Export& desc, std::vector<uint32_t>* out)
{
    if (!out || !importFrame(desc))
        return false;

    const size_t n = static_cast<size_t>(desc.width) * desc.height;
    std::vector<uint8_t> rgba(n * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glReadPixels(0, 0, static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height),
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    out->resize(n);
    for (uint32_t y = 0; y < desc.height; ++y) {
        const uint32_t srcY = desc.height - 1 - y;
        const uint8_t* row = rgba.data() + static_cast<size_t>(srcY) * desc.width * 4;
        for (uint32_t x = 0; x < desc.width; ++x) {
            const uint8_t* p = row + x * 4;
            (*out)[static_cast<size_t>(y) * desc.width + x]
                = (uint32_t(p[3]) << 24) | (uint32_t(p[0]) << 16)
                  | (uint32_t(p[1]) << 8) | uint32_t(p[2]);
        }
    }
    return true;
}

} // namespace dmabuf_window
