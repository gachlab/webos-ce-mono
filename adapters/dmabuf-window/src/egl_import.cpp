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

#include <gbm.h>

namespace dmabuf_window {

// Phase 1 samples via gbm_bo_import + CPU map. Mesa rejects
// glEGLImageTargetTexture2DOES(GL_TEXTURE_2D) for these linear BOs
// (EXTERNAL_OES + blit is the later compose path for HostWindowData).

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

} // namespace dmabuf_window
