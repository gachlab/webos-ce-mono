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

#include "Common.h"

#include "HostWindowDataDmaBuf.h"

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <vector>

#include <QImage>
#include <PIpcBuffer.h>

#include "Logging.h"

namespace {

bool exportFromHandoffBuffer(int key, dmabuf_window::Export* out)
{
	PIpcBuffer* buf = PIpcBuffer::attach(key);
	if (!buf || !buf->data()) {
		delete buf;
		return false;
	}

	dmabuf_window::Handoff handoff;
	memcpy(&handoff, buf->data(), sizeof(handoff));
	delete buf;

	if (handoff.magic != dmabuf_window::Handoff::kMagic || handoff.fd < 0)
		return false;

	const int peer = dmabuf_window::peerPid();
	const int fd = (peer > 0)
		? dmabuf_window::duplicateFdFromPeer(peer, handoff.fd)
		: -1;
	if (fd < 0)
		return false;

	out->fd = fd;
	out->width = handoff.width;
	out->height = handoff.height;
	out->stride = handoff.stride;
	out->offset = handoff.offset;
	out->fourcc = handoff.fourcc;
	out->modifier = handoff.modifier;
	return true;
}

} // namespace

HostWindowDataDmaBuf* HostWindowDataDmaBuf::createIfRegistered(int key, int metaDataKey,
															   int width, int height,
															   bool hasAlpha)
{
	dmabuf_window::Export desc;
	if (!dmabuf_window::registryTake(key, &desc)) {
		if (!exportFromHandoffBuffer(key, &desc))
			return 0;
	}
	return new HostWindowDataDmaBuf(key, metaDataKey, width, height, hasAlpha, desc);
}

HostWindowDataDmaBuf::HostWindowDataDmaBuf(int key, int metaDataKey, int width, int height,
										   bool hasAlpha, const dmabuf_window::Export& desc)
	: m_key(key)
	, m_metaDataBuffer(0)
	, m_width(width)
	, m_height(height)
	, m_hasAlpha(hasAlpha)
	, m_dirty(true)
	, m_desc(desc)
	, m_gl(dmabuf_window::GlImporter::create())
{
	if (metaDataKey >= 0)
		m_metaDataBuffer = PIpcBuffer::attach(metaDataKey);
}

HostWindowDataDmaBuf::~HostWindowDataDmaBuf()
{
	if (m_desc.fd >= 0) {
		::close(m_desc.fd);
		m_desc.fd = -1;
	}
	delete m_metaDataBuffer;
	m_metaDataBuffer = 0;
}

bool HostWindowDataDmaBuf::isValid() const
{
	return m_desc.fd >= 0;
}

void HostWindowDataDmaBuf::flip()
{
	int width = m_width;
	m_width = m_height;
	m_height = width;
	m_dirty = true;
}

void HostWindowDataDmaBuf::onUpdateRegion(QPixmap&, int, int, int, int)
{
	m_dirty = true;
}

bool HostWindowDataDmaBuf::acquireViaGl(QPixmap& screenPixmap)
{
	if (!m_gl || !m_gl->valid())
		return false;

	std::vector<uint32_t> pixels;
	if (!m_gl->copyToArgb32(m_desc, &pixels))
		return false;
	if (pixels.size() != static_cast<size_t>(m_width) * static_cast<size_t>(m_height))
		return false;

	QImage image(reinterpret_cast<const uchar*>(pixels.data()), m_width, m_height,
				 m_width * 4, QImage::Format_ARGB32_Premultiplied);
	screenPixmap = QPixmap::fromImage(image.copy());
	return true;
}

bool HostWindowDataDmaBuf::acquireViaMmap(QPixmap& screenPixmap)
{
	if (m_desc.fd < 0 || m_desc.stride == 0 || m_desc.height == 0)
		return false;

	const size_t bytes = static_cast<size_t>(m_desc.stride) * m_desc.height;
	void* ptr = ::mmap(nullptr, bytes, PROT_READ, MAP_SHARED, m_desc.fd, 0);
	if (ptr == MAP_FAILED)
		return false;

	QImage image(static_cast<const uchar*>(ptr), m_width, m_height,
				 static_cast<int>(m_desc.stride), QImage::Format_ARGB32_Premultiplied);
	screenPixmap = QPixmap::fromImage(image.copy());
	::munmap(ptr, bytes);
	return true;
}

QPixmap* HostWindowDataDmaBuf::acquirePixmap(QPixmap& screenPixmap)
{
	if (!m_dirty)
		return &screenPixmap;
	m_dirty = false;

	if (!acquireViaGl(screenPixmap))
		acquireViaMmap(screenPixmap);
	return &screenPixmap;
}
