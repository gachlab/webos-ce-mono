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

#ifndef MOUSETOTOUCH_H
#define MOUSETOTOUCH_H

#include <QtGlobal>

#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))

#include <QPointF>
#include <QTouchEvent>

class QMouseEvent;

//
// Turns a stream of mouse events into touch points a gesture can be read from.
//
// A touch point carries three positions: where the finger is, where it was on
// the previous event, and where it first went down. webOS reads gestures out of
// the differences:
//
//     CardWindowManager: p.scenePos() - p.startScenePos()   // drag a card
//                        p.scenePos() - p.lastScenePos()    // flick velocity
//     page, quicklaunchbar: the same three, to drag icons around
//
// Setting all three to the current position -- which is what the first version
// of the translation did -- makes both differences zero forever. Every gesture
// then looks like a finger that never moved: cards cannot be thrown off screen,
// icons cannot be dragged, and only taps work.
//
// So this remembers the previous and the initial position across the press,
// move and release of one drag. One finger only, id 0: a mouse has no more.
//
class MouseToTouch
{
public:
	MouseToTouch() : m_down(false) {}

	// Builds the point for one mouse event. Call with the state matching the
	// event: Pressed starts a new drag, Released ends it.
	QTouchEvent::TouchPoint translate(const QMouseEvent* event, Qt::TouchPointState state);

	// Forgets the drag in progress, so the next Moved is treated as a start
	// rather than a jump from wherever the last one ended.
	void reset() { m_down = false; }

private:
	bool m_down;
	QPointF m_startPos, m_startScenePos, m_startScreenPos;
	QPointF m_lastPos, m_lastScenePos, m_lastScreenPos;
};

#endif /* QT_VERSION >= 5 */

#endif /* MOUSETOTOUCH_H */
