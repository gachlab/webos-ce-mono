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

#include "QmlGraphicsSlot.h"

#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))

#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPainterPath>
#include <QTouchEvent>

#include "QmlSceneItem.h"
#include "TouchAsMouse.h"

QmlGraphicsSlot::QmlGraphicsSlot(QmlSceneItem* host, QQuickItem* place, QGraphicsObject* content)
	: QGraphicsObject(host)
	, m_host(host)
	, m_place(place)
	, m_content(content)
{
	setFlag(QGraphicsItem::ItemHasNoContents, true);
	setFlag(QGraphicsItem::ItemClipsChildrenToShape, true);
	setAcceptedMouseButtons(Qt::NoButton);
	setAcceptTouchEvents(true);

	if (m_content) {
		m_content->setParentItem(this);
		// HP's dashboard container accepts touch and drops it, which on a
		// device kept it from the QML around it while the same press also
		// arrived as a mouse event. Here nothing sends that mouse event but
		// this item, and a native item that takes the touch first keeps it
		// from ever getting here. Found live: the container swallowed every
		// swipe on a notification.
		m_content->setAcceptTouchEvents(false);
	}

	connect(host, SIGNAL(signalRendered()), this, SLOT(follow()));
	follow();
}

QRectF QmlGraphicsSlot::boundingRect() const
{
	return m_clip;
}

// Only where the native item is: a touch anywhere else in the clip belongs to
// the QML scene.
QPainterPath QmlGraphicsSlot::shape() const
{
	QPainterPath path;
	if (m_content)
		path.addRect(m_clip & m_content->mapRectToParent(m_content->boundingRect()));
	return path;
}

void QmlGraphicsSlot::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
	Q_UNUSED(painter);
	Q_UNUSED(option);
	Q_UNUSED(widget);
}

void QmlGraphicsSlot::follow()
{
	if (!m_host || !m_content)
		return;

	QQuickItem* root = m_host->rootItem();
	QQuickItem* place = m_place;
	if (!root || !place) {
		setVisible(false);
		return;
	}

	// What of the place shows: the rect of every clipping item from the place
	// up to the root, in the root's coordinates, which are the host's.
	QRectF clip = m_host->boundingRect();
	qreal opacity = 1.0;
	bool visible = true;
	for (QQuickItem* item = place; item; item = item->parentItem()) {
		if (item->clip())
			clip &= item->mapRectToItem(root, QRectF(0, 0, item->width(), item->height()));
		opacity *= item->opacity();
		visible = visible && item->isVisible();
		if (item == root)
			break;
	}

	if (clip != m_clip) {
		prepareGeometryChange();
		m_clip = clip;
	}
	m_content->setPos(place->mapToItem(root, QPointF(0, 0)));
	setOpacity(opacity);
	setVisible(visible);
}

bool QmlGraphicsSlot::sceneEvent(QEvent* event)
{
	switch (event->type()) {
	case QEvent::TouchBegin:
	case QEvent::TouchUpdate:
	case QEvent::TouchEnd:
	case QEvent::TouchCancel:
		if (!m_content || !TouchAsMouse::send(event, m_content))
			return false;
		event->accept();
		return true;
	default:
		return QGraphicsObject::sceneEvent(event);
	}
}

#endif /* QT_VERSION >= 5 */
