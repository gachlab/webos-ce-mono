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

#include "QmlSceneItem.h"

#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))

#include <glib.h>

#include <QCoreApplication>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QQuickRenderTarget>
#include <QtGui/private/qeventpoint_p.h>

void QmlSceneItem::setUpSoftwareBackend()
{
	// Respect an explicit choice, so the backend can still be forced from the
	// environment when debugging.
	if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
		// QQuickRenderControl sets up an RHI unless the graphics API is
		// Software, and the environment variable alone did not stop it: sync()
		// then refused with "can only sync when beginFrame() has been called".
		QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
}

QmlSceneItem::QmlSceneItem(QQmlComponent* component, QGraphicsItem* parent)
	: QGraphicsObject(parent)
	, m_control(0)
	, m_window(0)
	, m_root(0)
	, m_initialized(false)
{
	if (!component)
		return;

	m_control = new QQuickRenderControl(this);
	m_window = new QQuickWindow(m_control);
	m_window->setColor(Qt::transparent);

	// QQmlComponent::create() incubates asynchronously unless the engine has a
	// controller; give it the window's so nested Loaders and delegates finish.
	QQmlEngine* engine = component->engine();
	if (engine && !engine->incubationController())
		engine->setIncubationController(m_window->incubationController());

	QObject* created = component->create();
	if (!created) {
		g_warning("%s: QML component produced no object: %s", __PRETTY_FUNCTION__,
		          qPrintable(component->errorString()));
		return;
	}

	m_root = qobject_cast<QQuickItem*>(created);
	if (!m_root) {
		g_warning("%s: QML root is a %s, not an item", __PRETTY_FUNCTION__,
		          created->metaObject()->className());
		delete created;
		return;
	}

	m_root->setParent(m_window->contentItem());
	m_root->setParentItem(m_window->contentItem());

	setFlag(QGraphicsItem::ItemIsFocusable, true);
	setAcceptHoverEvents(true);
	// Without this QGraphicsScene never routes touch points to this item, and
	// touch is how input arrives in this shell.
	setAcceptTouchEvents(true);

	m_renderTimer.setSingleShot(true);
	m_renderTimer.setInterval(0);
	connect(&m_renderTimer, SIGNAL(timeout()), this, SLOT(renderNow()));

	// The render control asks for a new frame when the scene graph changed
	// (sceneChanged) and when an animation drives it (renderRequested).
	connect(m_control, SIGNAL(renderRequested()), this, SLOT(scheduleRender()));
	connect(m_control, SIGNAL(sceneChanged()), this, SLOT(scheduleRender()));

	connect(m_root, SIGNAL(widthChanged()), this, SLOT(syncSize()));
	connect(m_root, SIGNAL(heightChanged()), this, SLOT(syncSize()));

	// Existing code hides and shows the QML root object directly. Mirror that
	// onto the host, or the host stays in the scene and keeps swallowing mouse
	// events for a panel that is meant to be gone.
	//
	// Opacity is deliberately NOT mirrored: the root's opacity is already baked
	// into the grabbed image, so applying it again here would square it.
	connect(m_root, SIGNAL(visibleChanged()), this, SLOT(syncVisible()));
	setVisible(m_root->isVisible());

	syncSize();
	renderNow();
}

QmlSceneItem::~QmlSceneItem()
{
	// The root item belongs to the window's content item, which the window
	// deletes. Tear the graph down before the render control goes, or the scene
	// graph nodes outlive their renderer.
	delete m_window;
	m_window = 0;
	m_root = 0;
}

QRectF QmlSceneItem::boundingRect() const
{
	return QRectF(0, 0, m_size.width(), m_size.height());
}

void QmlSceneItem::syncSize()
{
	if (!m_root)
		return;

	QSizeF size(m_root->width(), m_root->height());
	if (size == m_size)
		return;

	prepareGeometryChange();
	m_size = size;
	m_window->setGeometry(0, 0, qMax(1, int(size.width())), qMax(1, int(size.height())));
	scheduleRender();
}

void QmlSceneItem::syncVisible()
{
	if (m_root)
		setVisible(m_root->isVisible());
}

void QmlSceneItem::scheduleRender()
{
	if (m_root && !m_renderTimer.isActive())
		m_renderTimer.start();
}

void QmlSceneItem::renderNow()
{
	if (!m_root || m_size.isEmpty())
		return;

	// The render control has no grab(): the software adaptation renders
	// straight into a paint device, so m_frame itself is the target.
	const QSize frameSize = m_size.toSize();
	//
	// The software renderer only repaints the regions that changed, so the
	// image is cleared once, when it is created, and never between frames: a
	// render with nothing dirty paints nothing, and clearing first left the
	// frame empty.
	if (m_frame.size() != frameSize) {
		m_frame = QImage(frameSize, QImage::Format_ARGB32_Premultiplied);
		m_frame.fill(Qt::transparent);
		m_window->setRenderTarget(QQuickRenderTarget::fromPaintDevice(&m_frame));
	}

	if (!m_initialized) {
		// With the software adaptation Qt 6 documents that initialize() must not
		// be called: it creates an RHI the software renderer cannot use ("QRhi
		// is only compatible with default adaptation"), and sync() and render()
		// then refuse to run outside a beginFrame()/endFrame() pair.
		//
		// Ask the window, not QQuickWindow::graphicsApi(): after
		// setGraphicsApi(Software) the static one still reports OpenGL, while
		// the window's renderer interface reports the adaptation in use.
		QSGRendererInterface* renderer = m_window->rendererInterface();
		if (!renderer || renderer->graphicsApi() != QSGRendererInterface::Software)
			m_control->initialize();
		m_initialized = true;
	}

	m_control->polishItems();
	m_control->sync();
	m_control->render();
	update();
}

void QmlSceneItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
	Q_UNUSED(option);
	Q_UNUSED(widget);

	if (!m_frame.isNull())
		painter->drawImage(QPointF(0, 0), m_frame);
}

bool QmlSceneItem::sceneEvent(QEvent* event)
{
	switch (event->type()) {
	case QEvent::TouchBegin:
	case QEvent::TouchUpdate:
	case QEvent::TouchEnd:
	case QEvent::TouchCancel:
		if (deliverTouch(static_cast<QTouchEvent*>(event)))
			return true;
		break;
	default:
		break;
	}
	return QGraphicsObject::sceneEvent(event);
}

bool QmlSceneItem::deliverTouch(QTouchEvent* event)
{
	if (!m_window || !m_root)
		return false;

	// Item coordinates are the offscreen window's coordinates: the QML root sits
	// at the window origin and this host's origin is its top left. QQuickWindow
	// hit tests on scenePos, so both are set to the item-local position.
	QList<QEventPoint> points = event->points();
	for (QEventPoint& p : points) {
		// The copies share their data with the event's own points, and
		// QMutableEventPoint's setters write through: detach first.
		QMutableEventPoint::detach(p);
		const QPointF local = p.position();
		QMutableEventPoint::setPosition(p, local);
		QMutableEventPoint::setScenePosition(p, local);
	}

	QTouchEvent translated(event->type(), event->pointingDevice(), event->modifiers(), points);
	translated.setAccepted(false);
	QCoreApplication::sendEvent(m_window, &translated);

	if (translated.isAccepted()) {
		event->accept();
		return true;
	}
	return false;
}

void QmlSceneItem::deliverMouse(QGraphicsSceneMouseEvent* event, QEvent::Type type)
{
	if (!m_window) {
		event->ignore();
		return;
	}

	// Item coordinates are already the offscreen window's coordinates: the root
	// item sits at the window origin and this object's origin is its top left.
	QPointF local = event->pos();
	QMouseEvent translated(type, local, local, event->screenPos(),
	                       event->button(), event->buttons(), event->modifiers());
	QCoreApplication::sendEvent(m_window, &translated);
	event->setAccepted(translated.isAccepted());
}

void QmlSceneItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
	deliverMouse(event, QEvent::MouseButtonPress);
}

void QmlSceneItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
	deliverMouse(event, QEvent::MouseMove);
}

void QmlSceneItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
	deliverMouse(event, QEvent::MouseButtonRelease);
}

void QmlSceneItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
	if (!m_window)
		return;

	QPointF local = event->pos();
	QMouseEvent translated(QEvent::MouseMove, local, local, event->screenPos(),
	                       Qt::NoButton, Qt::NoButton, event->modifiers());
	QCoreApplication::sendEvent(m_window, &translated);
}

void QmlSceneItem::keyPressEvent(QKeyEvent* event)
{
	if (m_window)
		QCoreApplication::sendEvent(m_window, event);
}

void QmlSceneItem::keyReleaseEvent(QKeyEvent* event)
{
	if (m_window)
		QCoreApplication::sendEvent(m_window, event);
}

#endif /* QT_VERSION >= 5 */
