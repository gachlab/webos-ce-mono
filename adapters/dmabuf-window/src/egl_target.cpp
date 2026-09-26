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

#include <unistd.h>

#include <vector>

#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#ifndef EGL_GL_TEXTURE_2D_KHR
#define EGL_GL_TEXTURE_2D_KHR 0x30B1
#endif

namespace dmabuf_window {
namespace {

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

PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC getExportQuery()
{
    static auto fn = reinterpret_cast<PFNEGLEXPORTDMABUFIMAGEQUERYMESAPROC>(
        eglGetProcAddress("eglExportDMABUFImageQueryMESA"));
    return fn;
}

PFNEGLEXPORTDMABUFIMAGEMESAPROC getExportImage()
{
    static auto fn = reinterpret_cast<PFNEGLEXPORTDMABUFIMAGEMESAPROC>(
        eglGetProcAddress("eglExportDMABUFImageMESA"));
    return fn;
}

} // namespace

std::unique_ptr<GlRenderTarget> GlRenderTarget::create(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return {};
    EGLDisplay dpy = eglGetCurrentDisplay();
    EGLContext ctx = eglGetCurrentContext();
    if (dpy == EGL_NO_DISPLAY || ctx == EGL_NO_CONTEXT)
        return {};
    if (!getCreateImage() || !getDestroyImage() || !getExportQuery() || !getExportImage())
        return {};

    auto target = std::unique_ptr<GlRenderTarget>(new GlRenderTarget);
    target->m_display = dpy;
    target->m_width = width;
    target->m_height = height;

    glGenTextures(1, &target->m_colorTex);
    glBindTexture(GL_TEXTURE_2D, target->m_colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(width),
                 static_cast<GLsizei>(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenFramebuffers(1, &target->m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, target->m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           target->m_colorTex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        target.reset();
        return {};
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!target->exportDmaBuf()) {
        target.reset();
        return {};
    }
    return target;
}

GlRenderTarget::~GlRenderTarget()
{
    if (m_export.fd >= 0) {
        ::close(m_export.fd);
        m_export.fd = -1;
    }
    if (m_fbo)
        glDeleteFramebuffers(1, &m_fbo);
    if (m_colorTex)
        glDeleteTextures(1, &m_colorTex);
    m_fbo = 0;
    m_colorTex = 0;
}

bool GlRenderTarget::exportDmaBuf()
{
    auto createImage = getCreateImage();
    auto destroyImage = getDestroyImage();
    auto exportQuery = getExportQuery();
    auto exportImage = getExportImage();
    auto* dpy = static_cast<EGLDisplay>(m_display);
    EGLContext ctx = eglGetCurrentContext();

    EGLImageKHR image = createImage(dpy, ctx, EGL_GL_TEXTURE_2D_KHR,
                                    reinterpret_cast<EGLClientBuffer>(static_cast<uintptr_t>(m_colorTex)),
                                    nullptr);
    if (image == EGL_NO_IMAGE_KHR)
        return false;

    int fourcc = 0;
    int nplanes = 0;
    EGLuint64KHR modifiers[4] = {};
    if (!exportQuery(dpy, image, &fourcc, &nplanes, modifiers) || nplanes < 1) {
        destroyImage(dpy, image);
        return false;
    }

    int fds[4] = { -1, -1, -1, -1 };
    EGLint strides[4] = {};
    EGLint offsets[4] = {};
    if (!exportImage(dpy, image, fds, strides, offsets) || fds[0] < 0) {
        destroyImage(dpy, image);
        return false;
    }
    destroyImage(dpy, image);

    if (m_export.fd >= 0)
        ::close(m_export.fd);
    m_export.fd = fds[0];
    m_export.width = m_width;
    m_export.height = m_height;
    m_export.stride = static_cast<uint32_t>(strides[0]);
    m_export.offset = static_cast<uint32_t>(offsets[0]);
    m_export.fourcc = static_cast<uint32_t>(fourcc);
    m_export.modifier = modifiers[0];
    for (int i = 1; i < 4; ++i) {
        if (fds[i] >= 0)
            ::close(fds[i]);
    }
    return true;
}

bool GlRenderTarget::begin()
{
    if (!m_fbo)
        return false;
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, static_cast<GLsizei>(m_width), static_cast<GLsizei>(m_height));
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

void GlRenderTarget::end()
{
    glFinish();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool GlRenderTarget::clearArgb(uint32_t argb)
{
    if (!begin())
        return false;
    const float a = float((argb >> 24) & 0xff) / 255.f;
    const float r = float((argb >> 16) & 0xff) / 255.f;
    const float g = float((argb >> 8) & 0xff) / 255.f;
    const float b = float(argb & 0xff) / 255.f;
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
    end();
    return true;
}

bool GlRenderTarget::uploadArgb32(const uint32_t* pixels, uint32_t stridePixels)
{
    if (!pixels || stridePixels < m_width || !m_colorTex)
        return false;
    if (!begin())
        return false;

    std::vector<uint8_t> rgba(static_cast<size_t>(m_width) * m_height * 4);
    for (uint32_t y = 0; y < m_height; ++y) {
        // GL texture row 0 is bottom; flip while converting ARGB→RGBA.
        const uint32_t srcY = m_height - 1 - y;
        const uint32_t* src = pixels + static_cast<size_t>(srcY) * stridePixels;
        uint8_t* dst = rgba.data() + static_cast<size_t>(y) * m_width * 4;
        for (uint32_t x = 0; x < m_width; ++x) {
            const uint32_t p = src[x];
            dst[x * 4 + 0] = static_cast<uint8_t>((p >> 16) & 0xff);
            dst[x * 4 + 1] = static_cast<uint8_t>((p >> 8) & 0xff);
            dst[x * 4 + 2] = static_cast<uint8_t>(p & 0xff);
            dst[x * 4 + 3] = static_cast<uint8_t>((p >> 24) & 0xff);
        }
    }
    glBindTexture(GL_TEXTURE_2D, m_colorTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(m_width),
                    static_cast<GLsizei>(m_height), GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    end();
    return true;
}

} // namespace dmabuf_window
