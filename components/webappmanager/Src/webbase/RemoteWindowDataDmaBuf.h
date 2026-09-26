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

#ifndef REMOTEWINDOWDATADMABUF_H
#define REMOTEWINDOWDATADMABUF_H

#include "Common.h"
#include "RemoteWindowData.h"

#include <memory>

#include <QImage>

class QPainter;
class QOpenGLContext;
class QOffscreenSurface;
class PIpcBuffer;

namespace dmabuf_window {
class GlRenderTarget;
}

// CE present path (#79 / #84): EGL FBO exported as dma-buf. Prefer redirecting
// QtWebEngine's QQuickWindow into the FBO texture (no staging QImage). Falls
// back to staging uploadArgb32 when bindPresentTexture is not ready yet.
class RemoteWindowDataDmaBuf : public RemoteWindowData
{
public:
	RemoteWindowDataDmaBuf(int width, int height, bool hasAlpha);
	virtual ~RemoteWindowDataDmaBuf();

	virtual bool isValid() const;
	virtual int key() const;
	virtual int width() const { return m_width; }
	virtual int height() const { return m_height; }
	virtual bool hasAlpha() const { return m_hasAlpha; }
	virtual bool needsClear() const { return false; }
	virtual bool supportsPartialUpdates() const { return false; }
	virtual void setWindowMetaDataBuffer(PIpcBuffer* metaDataBuffer);
	virtual void flip();

	virtual PGContext* renderingContext() { return 0; }
	virtual QPainter* qtRenderingContext();

	virtual void beginPaint();
	virtual void endPaint(bool preserveOnFlip, const QRect& rect, bool flipBuffers = true);
	virtual void sendWindowUpdate(int x, int y, int w, int h);

	virtual bool engineOwnsPresent() const { return m_enginePresent; }
	virtual bool ensureEnginePresent(QWebPage* page);

	virtual bool hasDirectRendering() const { return false; }
	virtual bool directRenderingAllowed(bool val) { return false; }

	virtual void resize(int newWidth, int newHeight);
	virtual void clear();

private:
	bool ensureGl();
	bool publishRegistry();
	void discardSurface();

	PIpcBuffer* m_keyBuffer;
	int m_width;
	int m_height;
	bool m_hasAlpha;
	bool m_enginePresent;
	QPainter* m_context;
	QImage m_staging;
	QOpenGLContext* m_glContext;
	QOffscreenSurface* m_glSurface;
	std::unique_ptr<dmabuf_window::GlRenderTarget> m_target;
	int m_heldFd;

	RemoteWindowDataDmaBuf(const RemoteWindowDataDmaBuf&);
	RemoteWindowDataDmaBuf& operator=(const RemoteWindowDataDmaBuf&);
};

#endif
