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

#ifndef HOSTWINDOWDATADMABUF_H
#define HOSTWINDOWDATADMABUF_H

#include "Common.h"
#include "HostWindowData.h"

#include <memory>

#include <QPixmap>
#include <PIpcBuffer.h>

#include <dmabuf_window.h>

// CE present path (#79): import a Remote's dma-buf. Prefer GL
// (EXTERNAL_OES → FBO → QImage); fall back to mmap if GL is unavailable.
class HostWindowDataDmaBuf : public HostWindowData
{
public:
	static HostWindowDataDmaBuf* createIfRegistered(int key, int metaDataKey,
													int width, int height,
													bool hasAlpha);

	virtual ~HostWindowDataDmaBuf();

	virtual bool isValid() const;
	virtual int key() const { return m_key; }
	virtual int width() const { return m_width; }
	virtual int height() const { return m_height; }
	virtual bool hasAlpha() const { return m_hasAlpha; }
	virtual void flip();
	virtual PIpcBuffer* metaDataBuffer() const { return m_metaDataBuffer; }
	virtual void initializePixmap(QPixmap& screenPixmap) {}
	virtual QPixmap* acquirePixmap(QPixmap& screenPixmap);
	virtual void allowUpdates(bool) {}
	virtual void onUpdateRegion(QPixmap& screenPixmap, int x, int y, int w, int h);
	virtual void onUpdateWindowRequest() {}
	virtual void updateFromAppDirectRenderingLayer(int, int, int) {}
	virtual void onAboutToSendSyncMessage() {}

private:
	HostWindowDataDmaBuf(int key, int metaDataKey, int width, int height,
						 bool hasAlpha, const dmabuf_window::Export& desc);

	bool acquireViaGl(QPixmap& screenPixmap);
	bool acquireViaMmap(QPixmap& screenPixmap);

	int m_key;
	PIpcBuffer* m_metaDataBuffer;
	int m_width;
	int m_height;
	bool m_hasAlpha;
	bool m_dirty;
	dmabuf_window::Export m_desc;
	std::unique_ptr<dmabuf_window::GlImporter> m_gl;

	HostWindowDataDmaBuf(const HostWindowDataDmaBuf&);
	HostWindowDataDmaBuf& operator=(const HostWindowDataDmaBuf&);
};

#endif
