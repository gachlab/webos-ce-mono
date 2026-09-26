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

#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

#include <gbm.h>

namespace dmabuf_window {
namespace {

int openRenderNode()
{
    // Prefer the primary render node; fall back to a few common indices.
    static const char* const kCandidates[] = {
        "/dev/dri/renderD128",
        "/dev/dri/renderD129",
        "/dev/dri/card0",
    };
    for (const char* path : kCandidates) {
        const int fd = ::open(path, O_RDWR | O_CLOEXEC);
        if (fd >= 0)
            return fd;
    }
    return -1;
}

} // namespace

std::shared_ptr<Device> Device::openDefault()
{
    const int fd = openRenderNode();
    if (fd < 0)
        return {};

    gbm_device* gbm = gbm_create_device(fd);
    if (!gbm) {
        ::close(fd);
        return {};
    }

    auto device = std::shared_ptr<Device>(new Device);
    device->m_drmFd = fd;
    device->m_gbm = gbm;
    return device;
}

Device::~Device()
{
    if (m_gbm) {
        gbm_device_destroy(static_cast<gbm_device*>(m_gbm));
        m_gbm = nullptr;
    }
    if (m_drmFd >= 0) {
        ::close(m_drmFd);
        m_drmFd = -1;
    }
}

bool available()
{
    auto device = Device::openDefault();
    return device && device->valid();
}

bool wantFactoryBackend()
{
    // Opt-in until live Host OES + Quick redirect is visually solid (#84
    // seat0: default-on showed card scanlines). WEBOS_DMABUF=1 enables.
    const char* env = std::getenv("WEBOS_DMABUF");
    if (!env || std::strcmp(env, "1") != 0)
        return false;
    return available();
}

} // namespace dmabuf_window
