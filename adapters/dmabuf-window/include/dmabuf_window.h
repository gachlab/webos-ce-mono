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

namespace dmabuf_window {

// DRM_FORMAT_ARGB8888 little-endian matches QImage::Format_ARGB32_Premultiplied
// byte order on little-endian hosts (B,G,R,A in memory).
struct Export {
    int fd = -1; // caller owns; close when done
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

    // CPU map for writing ARGB32 premultiplied rows. One map at a time.
    void* mapWrite(uint32_t* strideOut);
    void unmap();

    // Dup of the dma-buf fd plus layout. Caller closes Export::fd.
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

// Import a dma-buf and sample a pixel (test / Host path). Phase 1 uses
// gbm_bo_import + CPU map; EGLImage→GL texture is the later compose step.
class Importer {
public:
    static std::unique_ptr<Importer> create();
    ~Importer();

    bool valid() const { return static_cast<bool>(m_device); }

    // Returns false on import failure. On success, *argb is packed
    // 0xAARRGGBB for the sample at (x, y).
    bool samplePixel(const Export& desc, int x, int y, uint32_t* argb);

private:
    Importer() = default;
    std::shared_ptr<Device> m_device;
};

// WEBOS_DMABUF=1 and a usable render node. Live two-process sessions must not
// set this until SCM_RIGHTS fd passing exists (see docs/webcontent-dmabuf.md).
bool wantFactoryBackend();
bool available();

// Process-local handoff so HostWindowDataFactory can resolve a Remote's
// dma-buf from the integer key() (in-process tests only).
void registryPut(int key, const Export& desc);
bool registryTake(int key, Export* out);
void registryClear(int key);

} // namespace dmabuf_window

#endif
