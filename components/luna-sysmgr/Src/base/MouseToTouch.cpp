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

#include "MouseToTouch.h"

#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))

#include <QMouseEvent>
// QEventPoint is read-only in Qt 6; QMutableEventPoint is how Qt's own input
// code fills one in. It keeps a QPointer<QWindow>, so QWindow has to be complete.
#include <QWindow>
#include <QtGui/private/qeventpoint_p.h>

QTouchEvent::TouchPoint MouseToTouch::translate(const QMouseEvent* event, Qt::TouchPointState state)
{
	const QPointF pos = event->localPos();
	const QPointF scenePos = event->windowPos();
	const QPointF screenPos = event->screenPos();

	// A press starts a drag. So does a move with no press behind it, which
	// happens when the press was consumed before it got here -- treating it as
	// a start is better than reporting a jump from a stale position.
	if (state == Qt::TouchPointPressed || !m_down) {
		m_down = true;
		m_startPos = pos;
		m_startScenePos = scenePos;
		m_startScreenPos = screenPos;
		m_lastPos = pos;
		m_lastScenePos = scenePos;
		m_lastScreenPos = screenPos;
	}

	// Qt 6 derives the local and scene start and last positions from the
	// global ones, so those are the only ones stored.
	QTouchEvent::TouchPoint point = QMutableEventPoint::withTimeStamp(
		event->timestamp(), 0, QEventPoint::State(state), pos, scenePos, screenPos);
	QMutableEventPoint::setGlobalPressPosition(point, m_startScreenPos);
	QMutableEventPoint::setGlobalLastPosition(point, m_lastScreenPos);
	QMutableEventPoint::setPressure(point, state == Qt::TouchPointReleased ? 0.0 : 1.0);

	m_lastPos = pos;
	m_lastScenePos = scenePos;
	m_lastScreenPos = screenPos;

	if (state == Qt::TouchPointReleased)
		m_down = false;

	return point;
}

#endif /* QT_VERSION >= 5 */
