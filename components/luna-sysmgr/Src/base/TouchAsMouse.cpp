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

#include "TouchAsMouse.h"

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QHash>
#include <QTouchEvent>

namespace TouchAsMouse {

namespace {

bool mouseType(QEvent::Type touch, QEvent::Type* type)
{
	switch (touch) {
	case QEvent::TouchBegin:  *type = QEvent::GraphicsSceneMousePress; return true;
	case QEvent::TouchUpdate: *type = QEvent::GraphicsSceneMouseMove; return true;
	case QEvent::TouchEnd:
	case QEvent::TouchCancel: *type = QEvent::GraphicsSceneMouseRelease; return true;
	default:                  return false;
	}
}

// Which child each container's current touch went to. Items are not
// QObjects, so a removed item is dropped from here by hand (see forget).
QHash<QGraphicsItem*, QGraphicsItem*>& grabbers()
{
	static QHash<QGraphicsItem*, QGraphicsItem*> map;
	return map;
}

bool isInside(QGraphicsItem* item, QGraphicsItem* container)
{
	return item != container && container->isAncestorOf(item);
}

}

bool send(QEvent* event, QGraphicsItem* target)
{
	QEvent::Type type;
	if (!target || !target->scene() || !mouseType(event->type(), &type))
		return false;
	QTouchEvent* touch = static_cast<QTouchEvent*>(event);
	if (touch->points().isEmpty())
		return false;

	// One finger, the first, as a left button.
	const QEventPoint& point = touch->points().first();
	QGraphicsSceneMouseEvent mouse(type);
	mouse.setScenePos(point.scenePosition());
	mouse.setLastScenePos(point.sceneLastPosition());
	mouse.setPos(target->mapFromScene(point.scenePosition()));
	mouse.setLastPos(target->mapFromScene(point.sceneLastPosition()));
	mouse.setButtonDownScenePos(Qt::LeftButton, point.scenePressPosition());
	mouse.setButtonDownPos(Qt::LeftButton, target->mapFromScene(point.scenePressPosition()));
	mouse.setButton(type == QEvent::GraphicsSceneMouseMove ? Qt::NoButton : Qt::LeftButton);
	mouse.setButtons(type == QEvent::GraphicsSceneMouseRelease ? Qt::NoButton : Qt::LeftButton);
	mouse.setModifiers(touch->modifiers());
	mouse.setAccepted(event->type() != QEvent::TouchCancel);
	target->scene()->sendEvent(target, &mouse);
	return true;
}

bool sendToChild(QEvent* event, QGraphicsItem* container)
{
	QEvent::Type type;
	if (!container->scene() || !mouseType(event->type(), &type))
		return false;
	QTouchEvent* touch = static_cast<QTouchEvent*>(event);
	if (touch->points().isEmpty())
		return false;

	QGraphicsItem* target = 0;
	if (event->type() == QEvent::TouchBegin) {
		const QPointF at = touch->points().first().scenePosition();
		Q_FOREACH (QGraphicsItem* item, container->scene()->items(at, Qt::IntersectsItemShape, Qt::DescendingOrder)) {
			if (isInside(item, container) && item->isVisible() && item->isEnabled()
				&& (item->acceptedMouseButtons() & Qt::LeftButton)) {
				target = item;
				break;
			}
		}
		if (target)
			grabbers().insert(container, target);
		else
			grabbers().remove(container);
	} else {
		target = grabbers().value(container, 0);
		// Gone since the press, or moved elsewhere.
		if (target && !(container->scene()->items().contains(target) && isInside(target, container)))
			target = 0;
		if (type == QEvent::GraphicsSceneMouseRelease)
			grabbers().remove(container);
	}
	return target && send(event, target);
}

}
