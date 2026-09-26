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

#ifndef WEBOS_DMABUF_WINDOW_H
#define WEBOS_DMABUF_WINDOW_H

#include <cstdint>
#include <memory>
#include <vector>

namespace dmabuf_window {

struct Handoff {
    static constexpr uint32_t kMagic = 0x42414d44u; // 'DMAB' LE
    uint32_t magic = 0;
    int32_t fd = -1;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint32_t offset = 0;
    uint32_t fourcc = 0;
    uint64_t modifier = 0;
};

struct Export {
    int fd = -1;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint32_t offset = 0;
    uint32_t fourcc = 0;
    uint64_t modifier = 0;
};

class Device {
public:
    static std::shared_ptr<Device> openDefault();
    ~Device();

    void* gbmDevice() const { return m_gbm; }
    int drmFd() const { return m_drmFd; }
    bool valid() const { return m_gbm != nullptr; }

private:
    Device() = default;
    int m_drmFd = -1;
    void* m_gbm = nullptr;
};

class Frame {
public:
    static std::unique_ptr<Frame> create(const std::shared_ptr<Device>& device,
                                         uint32_t width, uint32_t height);
    ~Frame();

    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    uint32_t stride() const { return m_stride; }

    void* mapWrite(uint32_t* strideOut);
    void unmap();

    bool exportDesc(Export* out) const;

    void* bo() const { return m_bo; }

private:
    Frame() = default;
    std::shared_ptr<Device> m_device;
    void* m_bo = nullptr;
    void* m_map = nullptr;
    void* m_mapData = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_stride = 0;
    uint32_t m_mapStride = 0;
};

// CPU path: gbm_bo_import + map (phase 1).
class Importer {
public:
    static std::unique_ptr<Importer> create();
    ~Importer();

    bool valid() const { return static_cast<bool>(m_device); }

    bool samplePixel(const Export& desc, int x, int y, uint32_t* argb);

private:
    Importer() = default;
    std::shared_ptr<Device> m_device;
};

// GL path: EGLImage + TEXTURE_EXTERNAL_OES → blit to RGBA FBO → readback.
// Matches the shell's OpenGL compose stack; avoids mmap of the BO on import.
class GlImporter {
public:
    static std::unique_ptr<GlImporter> create();
    ~GlImporter();

    bool valid() const { return m_display != nullptr; }

    // 0xAARRGGBB at (x,y).
    bool samplePixel(const Export& desc, int x, int y, uint32_t* argb);

    // Full frame as tightly packed ARGB32 premultiplied (width*height uint32_t).
    bool copyToArgb32(const Export& desc, std::vector<uint32_t>* out);

private:
    GlImporter() = default;
    bool ensureProgram();
    bool blitToFbo(const Export& desc);

    void* m_display = nullptr; // EGLDisplay
    void* m_context = nullptr; // EGLContext
    unsigned m_program = 0;
    unsigned m_extTex = 0;
    unsigned m_colorTex = 0;
    unsigned m_fbo = 0;
    uint32_t m_fboW = 0;
    uint32_t m_fboH = 0;
};

bool wantFactoryBackend();
bool available();

void registryPut(int key, const Export& desc);
bool registryTake(int key, Export* out);
void registryClear(int key);

int duplicateFdFromPeer(int peerPid, int remoteFd);

void setPeerPid(int pid);
int peerPid();

} // namespace dmabuf_window

#endif
