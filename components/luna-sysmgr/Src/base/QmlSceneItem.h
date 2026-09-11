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

#ifndef QMLSCENEITEM_H
#define QMLSCENEITEM_H

#include <QtGlobal>

#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))

#include <QGraphicsObject>
#include <QImage>
#include <QQuickItem>   // QPointer needs the complete type
#include <QTouchEvent>
#include <QPointer>
#include <QTimer>

class QQmlComponent;
class QQuickWindow;
class QQuickRenderControl;

//
// Hosts a QtQuick 2 scene inside the shell's QGraphicsScene.
//
// The shell is built on QGraphicsView: every card, panel and status bar item is
// a QGraphicsObject. QML 1 items were QGraphicsObjects too, so the old code
// parented them directly into that scene. QtQuick 2 items are not -- they only
// draw through a QQuickWindow's scene graph.
//
// So this class is the seam. It is a QGraphicsObject the scene understands, and
// it owns a QQuickWindow that is never shown. QQuickRenderControl drives that
// window's scene graph by hand, the result is grabbed as a QImage, and paint()
// blits it. Input travels the other way: scene events are translated back into
// window events and posted to the offscreen window, so the QML keeps its own
// hit testing, focus and hover handling.
//
// Callers keep talking to the QML root object itself through rootItem(), which
// is where property(), connect() and findChild() still work exactly as before.
//
// The scene graph runs on the software backend (see setUpSoftwareBackend). The
// GL backend would need a shared context and an FBO round trip for what are
// menus and dialogs; the software rasteriser produces a QImage directly, has no
// context to lose, and costs nothing while nothing is animating.
//
class QmlSceneItem : public QGraphicsObject
{
	Q_OBJECT

public:
	// Instantiates component and hosts its root item. If the QML fails to load
	// or its root is not an item, rootItem() is null and the object draws
	// nothing -- callers already test for that.
	explicit QmlSceneItem(QQmlComponent* component, QGraphicsItem* parent = 0);
	virtual ~QmlSceneItem();

	QQuickItem* rootItem() const { return m_root; }

	virtual QRectF boundingRect() const;
	virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget);

	// Must be called before the QGuiApplication exists, because the scene graph
	// backend is chosen the first time QtQuick is used and never revisited.
	static void setUpSoftwareBackend();

protected:
	// The shell turns mouse input into touch before it reaches the scene (see
	// WindowServer::deliverAsTouch) and MouseEventEater swallows what is left,
	// so touch is the path that actually carries user input here. Mouse is kept
	// for the cases that still deliver it.
	virtual bool sceneEvent(QEvent* event);
	virtual void mousePressEvent(QGraphicsSceneMouseEvent* event);
	virtual void mouseMoveEvent(QGraphicsSceneMouseEvent* event);
	virtual void mouseReleaseEvent(QGraphicsSceneMouseEvent* event);
	virtual void hoverMoveEvent(QGraphicsSceneHoverEvent* event);
	virtual void keyPressEvent(QKeyEvent* event);
	virtual void keyReleaseEvent(QKeyEvent* event);

private Q_SLOTS:
	void scheduleRender();
	void renderNow();
	void syncSize();
	void syncVisible();

private:
	void deliverMouse(QGraphicsSceneMouseEvent* event, QEvent::Type type);
	bool deliverTouch(QTouchEvent* event);

	QQuickRenderControl* m_control;
	QQuickWindow* m_window;
	QPointer<QQuickItem> m_root;
	QImage m_frame;
	QTimer m_renderTimer;
	QSizeF m_size;
	bool m_initialized;
};

#endif /* QT_VERSION >= 5 */

#endif /* QMLSCENEITEM_H */
