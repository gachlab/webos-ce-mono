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

class QPainter;
class QImage;
class PIpcBuffer;

// CE present path (#79): pixels in a GBM dma-buf; key() is a small PIpcBuffer
// used only as the window identity for IPC / the in-process registry.
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

	virtual bool hasDirectRendering() const { return false; }
	virtual bool directRenderingAllowed(bool val) { return false; }

	virtual void resize(int newWidth, int newHeight);
	virtual void clear();

private:
	bool publishRegistry();
	void discardSurface();

	PIpcBuffer* m_keyBuffer;
	int m_width;
	int m_height;
	bool m_hasAlpha;
	QPainter* m_context;
	QImage* m_surface;
	void* m_device; // dmabuf_window::Device* shared via heap shared_ptr
	void* m_frame;  // dmabuf_window::Frame*
	uint32_t m_mapStride;
	void* m_mapPtr;

	RemoteWindowDataDmaBuf(const RemoteWindowDataDmaBuf&);
	RemoteWindowDataDmaBuf& operator=(const RemoteWindowDataDmaBuf&);
};

#endif
