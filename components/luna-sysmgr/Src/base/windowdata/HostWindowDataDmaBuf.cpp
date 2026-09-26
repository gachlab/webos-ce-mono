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

#include <sys/mman.h>
#include <unistd.h>

#include <QImage>

#include "Logging.h"

HostWindowDataDmaBuf* HostWindowDataDmaBuf::createIfRegistered(int key, int metaDataKey,
															   int width, int height,
															   bool hasAlpha)
{
	dmabuf_window::Export desc;
	if (!dmabuf_window::registryTake(key, &desc))
		return 0;
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

QPixmap* HostWindowDataDmaBuf::acquirePixmap(QPixmap& screenPixmap)
{
	if (!m_dirty)
		return &screenPixmap;
	m_dirty = false;

	if (m_desc.fd < 0 || m_desc.stride == 0 || m_desc.height == 0)
		return &screenPixmap;

	const size_t bytes = static_cast<size_t>(m_desc.stride) * m_desc.height;
	void* ptr = ::mmap(nullptr, bytes, PROT_READ, MAP_SHARED, m_desc.fd, 0);
	if (ptr == MAP_FAILED)
		return &screenPixmap;

	QImage image(static_cast<const uchar*>(ptr), m_width, m_height,
				 static_cast<int>(m_desc.stride), QImage::Format_ARGB32_Premultiplied);
	screenPixmap = QPixmap::fromImage(image.copy());
	::munmap(ptr, bytes);
	return &screenPixmap;
}
