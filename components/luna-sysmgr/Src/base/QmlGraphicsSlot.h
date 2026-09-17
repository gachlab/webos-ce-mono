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

#ifndef QMLGRAPHICSSLOT_H
#define QMLGRAPHICSSLOT_H

#include <QtGlobal>

#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))

#include <QGraphicsObject>
#include <QPointer>
#include <QQuickItem>

class QmlSceneItem;

//
// Shows a QGraphicsObject where a hosted QML scene keeps a place for it.
//
// Under QML 1 the shell handed native items to its QML, which made them
// children of its own items: the dashboard menu's
//
//     mainMenuItem.children: [DashboardContainer]
//
// put the notification container inside the menu's scrolling area. A QtQuick 2
// item cannot have a QGraphicsItem as a child, so that line only logged
// "Cannot append DashboardWindowContainer to a QML list".
//
// This item is the other half. It sits in the QmlSceneItem's own graphics
// subtree, holds the native item, and follows the QML item named as the place:
// its position, the clipping of the items around it, its visibility and its
// opacity -- which the QML scene bakes into its image and so never reaches a
// graphics item. It follows after every frame the scene renders, which is when
// any of those can have changed.
//
// Input on the native item reaches it as mouse events. The shell's input is
// touch (see WindowServer::deliverAsTouch), and QGraphicsScene hands a touch to
// the topmost item that accepts touch -- here the QML scene underneath, since
// HP's native items never asked for touch. On a device nobody accepted it, Qt
// made a mouse event of it, and that is what those items handle. So this item
// takes the touches that land on the native item and sends it that mouse
// event itself. The QML scene gets the rest.
//
// What it cannot do is let the QML take input away from the native item: a
// Flickable around the place no longer scrolls when the drag starts on the
// native item.
//
class QmlGraphicsSlot : public QGraphicsObject
{
	Q_OBJECT

public:
	QmlGraphicsSlot(QmlSceneItem* host, QQuickItem* place, QGraphicsObject* content);

	virtual QRectF boundingRect() const;
	virtual QPainterPath shape() const;
	virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget);

protected:
	virtual bool sceneEvent(QEvent* event);

public Q_SLOTS:
	void follow();

private:
	QPointer<QmlSceneItem> m_host;
	QPointer<QQuickItem> m_place;
	QPointer<QGraphicsObject> m_content;
	QRectF m_clip;
};

#endif /* QT_VERSION >= 5 */

#endif /* QMLGRAPHICSSLOT_H */
