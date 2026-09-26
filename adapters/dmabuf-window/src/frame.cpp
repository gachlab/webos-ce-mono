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

#include <drm_fourcc.h>
#include <gbm.h>

namespace dmabuf_window {

std::unique_ptr<Frame> Frame::create(const std::shared_ptr<Device>& device,
                                     uint32_t width, uint32_t height)
{
    if (!device || !device->valid() || width == 0 || height == 0)
        return {};

    auto* gbm = static_cast<gbm_device*>(device->gbmDevice());
    // Linear + rendering: USE_WRITE is rejected on this Mesa for ARGB8888
    // linear BOs; gbm_bo_map(..., TRANSFER_WRITE) still works.
    gbm_bo* bo = gbm_bo_create(gbm, width, height, GBM_FORMAT_ARGB8888,
                               GBM_BO_USE_LINEAR | GBM_BO_USE_RENDERING);
    if (!bo)
        return {};

    auto frame = std::unique_ptr<Frame>(new Frame);
    frame->m_device = device;
    frame->m_bo = bo;
    frame->m_width = width;
    frame->m_height = height;
    frame->m_stride = gbm_bo_get_stride(bo);
    return frame;
}

Frame::~Frame()
{
    if (m_map)
        unmap();
    if (m_bo) {
        gbm_bo_destroy(static_cast<gbm_bo*>(m_bo));
        m_bo = nullptr;
    }
}

void* Frame::mapWrite(uint32_t* strideOut)
{
    if (!m_bo || m_map)
        return nullptr;

    uint32_t stride = 0;
    void* mapData = nullptr;
    void* ptr = gbm_bo_map(static_cast<gbm_bo*>(m_bo), 0, 0, m_width, m_height,
                           GBM_BO_TRANSFER_WRITE, &stride, &mapData);
    if (!ptr)
        return nullptr;

    m_map = ptr;
    m_mapData = mapData;
    m_mapStride = stride;
    if (strideOut)
        *strideOut = stride;
    return ptr;
}

void Frame::unmap()
{
    if (!m_bo || !m_map)
        return;
    gbm_bo_unmap(static_cast<gbm_bo*>(m_bo), m_mapData);
    m_map = nullptr;
    m_mapData = nullptr;
    m_mapStride = 0;
}

bool Frame::exportDesc(Export* out) const
{
    if (!out || !m_bo)
        return false;

    const int fd = gbm_bo_get_fd(static_cast<gbm_bo*>(m_bo));
    if (fd < 0)
        return false;

    out->fd = fd;
    out->width = m_width;
    out->height = m_height;
    out->stride = m_stride;
    out->offset = 0;
    out->fourcc = DRM_FORMAT_ARGB8888;
    out->modifier = gbm_bo_get_modifier(static_cast<gbm_bo*>(m_bo));
    return true;
}

} // namespace dmabuf_window
