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

    const char* exts = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (!exts || !std::strstr(exts, "GL_OES_EGL_image_external")) {
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(dpy, ctx);
        eglTerminate(dpy);
        return {};
    }

    auto importer = std::unique_ptr<GlImporter>(new GlImporter);
    importer->m_display = dpy;
    importer->m_context = ctx;
    if (!importer->ensureProgram()) {
        importer.reset();
        return {};
    }
    return importer;
}

GlImporter::~GlImporter()
{
    auto* dpy = static_cast<EGLDisplay>(m_display);
    auto* ctx = static_cast<EGLContext>(m_context);
    if (dpy && ctx)
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
    if (m_fbo)
        glDeleteFramebuffers(1, &m_fbo);
    if (m_colorTex)
        glDeleteTextures(1, &m_colorTex);
    if (m_extTex)
        glDeleteTextures(1, &m_extTex);
    if (m_program)
        glDeleteProgram(m_program);
    if (dpy) {
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (ctx)
            eglDestroyContext(dpy, ctx);
        eglTerminate(dpy);
    }
    m_display = nullptr;
    m_context = nullptr;
}

bool GlImporter::ensureProgram()
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
    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glBindAttribLocation(m_program, 0, "aPos");
    glLinkProgram(m_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }
    glGenTextures(1, &m_extTex);
    return true;
}

bool GlImporter::blitToFbo(const Export& desc)
{
    if (!valid() || desc.fd < 0 || desc.width == 0 || desc.height == 0)
        return false;

    auto createImage = getCreateImage();
    auto destroyImage = getDestroyImage();
    auto imageTarget = getImageTargetTexture();
    if (!createImage || !destroyImage || !imageTarget)
        return false;

    auto* dpy = static_cast<EGLDisplay>(m_display);
    auto* ctx = static_cast<EGLContext>(m_context);
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
        return false;

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
        return false;
    }

    glViewport(0, 0, static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height));
    glUseProgram(m_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, m_extTex);
    glUniform1i(glGetUniformLocation(m_program, "uTex"), 0);
    static const GLfloat verts[] = { -1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f };
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, verts);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);

    destroyImage(dpy, image);
    return true;
}

bool GlImporter::samplePixel(const Export& desc, int x, int y, uint32_t* argb)
{
    if (!argb || x < 0 || y < 0
        || static_cast<uint32_t>(x) >= desc.width
        || static_cast<uint32_t>(y) >= desc.height)
        return false;
    if (!blitToFbo(desc))
        return false;

    GLubyte px[4] = {0, 0, 0, 0};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    // RGBA readback → 0xAARRGGBB (matches DRM ARGB8888 CPU fill on LE).
    *argb = (uint32_t(px[3]) << 24) | (uint32_t(px[0]) << 16)
            | (uint32_t(px[1]) << 8) | uint32_t(px[2]);
    return true;
}

bool GlImporter::copyToArgb32(const Export& desc, std::vector<uint32_t>* out)
{
    if (!out || !blitToFbo(desc))
        return false;

    const size_t n = static_cast<size_t>(desc.width) * desc.height;
    std::vector<uint8_t> rgba(n * 4);
    glReadPixels(0, 0, static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height),
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    out->resize(n);
    // GLES read is bottom-up; flip to top-left origin like QImage.
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
