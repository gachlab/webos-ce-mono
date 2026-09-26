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

#include <PIpcBuffer.h>
#include <PIpcChannel.h>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QPainter>
#include <QSurfaceFormat>

#include <dmabuf_window.h>

#define MESSAGES_INTERNAL_FILE "SysMgrMessagesInternal.h"
#include <PIpcMessageMacros.h>

#include "Logging.h"
#include "WindowMetaData.h"

RemoteWindowDataDmaBuf::RemoteWindowDataDmaBuf(int width, int height, bool hasAlpha)
	: m_keyBuffer(0)
	, m_width(width)
	, m_height(height)
	, m_hasAlpha(hasAlpha)
	, m_context(0)
	, m_glContext(0)
	, m_glSurface(0)
	, m_heldFd(-1)
{
	m_keyBuffer = PIpcBuffer::create(static_cast<int>(sizeof(dmabuf_window::Handoff)));
	if (!m_keyBuffer)
		return;
	m_staging = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
	if (!ensureGl())
		return;
	publishRegistry();
}

RemoteWindowDataDmaBuf::~RemoteWindowDataDmaBuf()
{
	if (m_keyBuffer)
		dmabuf_window::registryClear(m_keyBuffer->key());
	discardSurface();
	if (m_heldFd >= 0) {
		::close(m_heldFd);
		m_heldFd = -1;
	}
	delete m_keyBuffer;
	m_keyBuffer = 0;
}

bool RemoteWindowDataDmaBuf::isValid() const
{
	return m_keyBuffer && m_target && m_target->valid() && m_heldFd >= 0
		&& !m_staging.isNull();
}

int RemoteWindowDataDmaBuf::key() const
{
	return m_keyBuffer ? m_keyBuffer->key() : -1;
}

void RemoteWindowDataDmaBuf::setWindowMetaDataBuffer(PIpcBuffer* metaDataBuffer)
{
	m_metaDataBuffer = metaDataBuffer;
}

bool RemoteWindowDataDmaBuf::ensureGl()
{
	if (m_target && m_target->valid())
		return true;

	if (!m_glContext) {
		auto tryCreate = [&](QSurfaceFormat::RenderableType rt, int maj, int min) -> bool {
			QSurfaceFormat fmt;
			fmt.setRenderableType(rt);
			fmt.setVersion(maj, min);
			fmt.setAlphaBufferSize(8);
			fmt.setRedBufferSize(8);
			fmt.setGreenBufferSize(8);
			fmt.setBlueBufferSize(8);

			delete m_glSurface;
			m_glSurface = new QOffscreenSurface;
			m_glSurface->setFormat(fmt);
			m_glSurface->create();
			if (!m_glSurface->isValid())
				return false;

			delete m_glContext;
			m_glContext = new QOpenGLContext;
			m_glContext->setFormat(fmt);
			return m_glContext->create();
		};

		if (!tryCreate(QSurfaceFormat::OpenGLES, 2, 0)
			&& !tryCreate(QSurfaceFormat::OpenGL, 2, 1)) {
			g_critical("%s: QOpenGLContext::create failed (GLES2 and GL2.1)\n",
					   __PRETTY_FUNCTION__);
			return false;
		}
	}

	if (!m_glContext->makeCurrent(m_glSurface)) {
		g_critical("%s: makeCurrent failed\n", __PRETTY_FUNCTION__);
		return false;
	}

	m_target = dmabuf_window::GlRenderTarget::create(
		static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height));
	if (!m_target) {
		g_critical("%s: GlRenderTarget::create failed\n", __PRETTY_FUNCTION__);
		m_glContext->doneCurrent();
		return false;
	}
	m_glContext->doneCurrent();
	return true;
}

bool RemoteWindowDataDmaBuf::publishRegistry()
{
	if (!m_target || !m_keyBuffer)
		return false;

	const dmabuf_window::Export& desc = m_target->exportDesc();
	if (desc.fd < 0)
		return false;

	if (m_heldFd >= 0 && m_heldFd != desc.fd)
		::close(m_heldFd);
	m_heldFd = ::dup(desc.fd);
	if (m_heldFd < 0)
		return false;

	dmabuf_window::Export published = desc;
	published.fd = m_heldFd;

	dmabuf_window::Handoff handoff;
	handoff.magic = dmabuf_window::Handoff::kMagic;
	handoff.fd = m_heldFd;
	handoff.width = published.width;
	handoff.height = published.height;
	handoff.stride = published.stride;
	handoff.offset = published.offset;
	handoff.fourcc = published.fourcc;
	handoff.modifier = published.modifier;
	memcpy(m_keyBuffer->data(), &handoff, sizeof(handoff));

	dmabuf_window::registryPut(m_keyBuffer->key(), published);
	return true;
}

void RemoteWindowDataDmaBuf::discardSurface()
{
	if (m_glContext && m_glSurface)
		m_glContext->makeCurrent(m_glSurface);
	delete m_context;
	m_context = 0;
	m_target.reset();
	if (m_glContext)
		m_glContext->doneCurrent();
	delete m_glContext;
	m_glContext = 0;
	delete m_glSurface;
	m_glSurface = 0;
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
	if (m_staging.isNull() || !ensureGl())
		return 0;
	m_context = new QPainter;
	return m_context;
}

void RemoteWindowDataDmaBuf::beginPaint()
{
	luna_assert(m_context && !m_staging.isNull());
	m_context->begin(&m_staging);
}

void RemoteWindowDataDmaBuf::endPaint(bool, const QRect&, bool)
{
	if (m_context && m_context->isActive())
		m_context->end();

	if (!m_target || !m_glContext || !m_glSurface)
		return;
	if (!m_glContext->makeCurrent(m_glSurface))
		return;
	const auto* bits = reinterpret_cast<const uint32_t*>(m_staging.constBits());
	const uint32_t stride = static_cast<uint32_t>(m_staging.bytesPerLine() / 4);
	m_target->uploadArgb32(bits, stride);
	m_glContext->doneCurrent();
}

void RemoteWindowDataDmaBuf::sendWindowUpdate(int x, int y, int w, int h)
{
	luna_assert(m_channel);
	m_channel->sendAsyncMessage(new ViewHost_UpdateWindowRegion(key(), x, y, w, h));
}

void RemoteWindowDataDmaBuf::resize(int newWidth, int newHeight)
{
	if (m_keyBuffer)
		dmabuf_window::registryClear(m_keyBuffer->key());
	if (m_heldFd >= 0) {
		::close(m_heldFd);
		m_heldFd = -1;
	}

	m_width = newWidth;
	m_height = newHeight;
	m_staging = QImage(newWidth, newHeight, QImage::Format_ARGB32_Premultiplied);

	delete m_context;
	m_context = 0;
	if (m_glContext && m_glSurface)
		m_glContext->makeCurrent(m_glSurface);
	m_target.reset();
	if (m_glContext)
		m_glContext->doneCurrent();

	if (!ensureGl())
		return;
	publishRegistry();
}

void RemoteWindowDataDmaBuf::clear()
{
	if (!qtRenderingContext())
		return;
	beginPaint();
	m_context->setCompositionMode(QPainter::CompositionMode_Source);
	m_context->fillRect(QRect(0, 0, m_width, m_height),
						m_hasAlpha ? Qt::transparent : Qt::white);
	endPaint(false, QRect(), false);
	sendWindowUpdate(0, 0, m_width, m_height);
}
