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

#include "RemoteWindowDataDmaBuf.h"

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <memory>

#include <PIpcBuffer.h>
#include <PIpcChannel.h>
#include <QImage>
#include <QPainter>

#include <dmabuf_window.h>

#define MESSAGES_INTERNAL_FILE "SysMgrMessagesInternal.h"
#include <PIpcMessageMacros.h>

#include "Logging.h"
#include "WindowMetaData.h"

namespace {

using DevicePtr = std::shared_ptr<dmabuf_window::Device>;

DevicePtr* asDevice(void* p)
{
	return static_cast<DevicePtr*>(p);
}

dmabuf_window::Frame* asFrame(void* p)
{
	return static_cast<dmabuf_window::Frame*>(p);
}

} // namespace

RemoteWindowDataDmaBuf::RemoteWindowDataDmaBuf(int width, int height, bool hasAlpha)
	: m_keyBuffer(0)
	, m_width(width)
	, m_height(height)
	, m_hasAlpha(hasAlpha)
	, m_context(0)
	, m_surface(0)
	, m_device(0)
	, m_frame(0)
	, m_mapStride(0)
	, m_mapPtr(0)
{
	m_keyBuffer = PIpcBuffer::create(64);
	if (!m_keyBuffer)
		return;

	auto device = dmabuf_window::Device::openDefault();
	if (!device || !device->valid())
		return;

	auto frame = dmabuf_window::Frame::create(device,
		static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	if (!frame)
		return;

	m_device = new DevicePtr(device);
	m_frame = frame.release();
	publishRegistry();
}

RemoteWindowDataDmaBuf::~RemoteWindowDataDmaBuf()
{
	if (m_keyBuffer)
		dmabuf_window::registryClear(m_keyBuffer->key());
	discardSurface();
	delete asFrame(m_frame);
	m_frame = 0;
	delete asDevice(m_device);
	m_device = 0;
	delete m_keyBuffer;
	m_keyBuffer = 0;
}

bool RemoteWindowDataDmaBuf::isValid() const
{
	return m_keyBuffer && m_frame;
}

int RemoteWindowDataDmaBuf::key() const
{
	return m_keyBuffer ? m_keyBuffer->key() : -1;
}

void RemoteWindowDataDmaBuf::setWindowMetaDataBuffer(PIpcBuffer* metaDataBuffer)
{
	m_metaDataBuffer = metaDataBuffer;
}

bool RemoteWindowDataDmaBuf::publishRegistry()
{
	if (!m_frame || !m_keyBuffer)
		return false;
	dmabuf_window::Export desc;
	if (!asFrame(m_frame)->exportDesc(&desc))
		return false;
	dmabuf_window::registryPut(m_keyBuffer->key(), desc);
	::close(desc.fd);
	return true;
}

void RemoteWindowDataDmaBuf::discardSurface()
{
	delete m_context;
	m_context = 0;
	delete m_surface;
	m_surface = 0;
	if (m_frame && m_mapPtr) {
		asFrame(m_frame)->unmap();
		m_mapPtr = 0;
		m_mapStride = 0;
	}
}

void RemoteWindowDataDmaBuf::flip()
{
	int width = m_width;
	m_width = m_height;
	m_height = width;
	resize(m_width, m_height);
}

QPainter* RemoteWindowDataDmaBuf::qtRenderingContext()
{
	if (m_context)
		return m_context;
	if (!m_frame)
		return 0;

	uint32_t stride = 0;
	void* ptr = asFrame(m_frame)->mapWrite(&stride);
	if (!ptr)
		return 0;
	m_mapPtr = ptr;
	m_mapStride = stride;
	m_surface = new QImage(reinterpret_cast<uchar*>(ptr), m_width, m_height,
						   static_cast<int>(stride),
						   QImage::Format_ARGB32_Premultiplied);
	m_context = new QPainter;
	return m_context;
}

void RemoteWindowDataDmaBuf::beginPaint()
{
	luna_assert(m_context && m_surface);
	m_context->begin(m_surface);
}

void RemoteWindowDataDmaBuf::endPaint(bool, const QRect&, bool)
{
	if (m_context)
		m_context->end();
}

void RemoteWindowDataDmaBuf::sendWindowUpdate(int x, int y, int w, int h)
{
	luna_assert(m_channel);
	m_channel->sendAsyncMessage(new ViewHost_UpdateWindowRegion(key(), x, y, w, h));
}

void RemoteWindowDataDmaBuf::resize(int newWidth, int newHeight)
{
	if (!m_device || !asDevice(m_device) || !*asDevice(m_device))
		return;

	if (m_keyBuffer)
		dmabuf_window::registryClear(m_keyBuffer->key());
	discardSurface();
	delete asFrame(m_frame);
	m_frame = 0;

	m_width = newWidth;
	m_height = newHeight;
	auto frame = dmabuf_window::Frame::create(*asDevice(m_device),
		static_cast<uint32_t>(newWidth), static_cast<uint32_t>(newHeight));
	if (!frame)
		return;
	m_frame = frame.release();
	publishRegistry();
}

void RemoteWindowDataDmaBuf::clear()
{
	if (!qtRenderingContext())
		return;
	beginPaint();
	m_surface->fill(m_hasAlpha ? Qt::transparent : Qt::white);
	endPaint(false, QRect(), false);
	sendWindowUpdate(0, 0, m_width, m_height);
}
