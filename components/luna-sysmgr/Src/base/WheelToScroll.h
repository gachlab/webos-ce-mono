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

#ifndef WHEELTOSCROLL_H
#define WHEELTOSCROLL_H

#include <QObject>

class QWidget;

//
// Turns the scroll wheel into something a webOS card can be told about.
//
// webOS had no wheel, so nothing in HP's code picks one up: a QWheelEvent
// arrives at the shell's viewport, QGraphicsView finds nobody who wants it, and
// it is dropped. This filter takes it there and sends the active card a scroll
// event through the range HP reserved for events he did not define, packed by
// adapters/input-compat.
//
// Installed exactly like MouseEventEater, from Main.cpp, and for the same
// reason it watches one widget: the filter goes on the QCoreApplication, so
// without watch() it would answer for wheels aimed at any widget in the
// process, including the gesture strip's buttons.
//
// Why here rather than in CardWindow::wheelEvent, which would get the target
// and its coordinates from the scene for free: QGraphicsSceneWheelEvent carries
// only delta() and orientation() in Qt 6, with nowhere to put pixelDelta, so a
// trackpad's high-resolution scroll is already gone by the time the item is
// reached. MEASURED in tests/wheel-in: a pixelDelta-only event arrives there as
// delta 0. Here the whole QWheelEvent is still intact.
//
// No Q_OBJECT: there are no signals or slots, only an override of a virtual
// QObject already has, and leaving it out keeps the class out of moc's way.
//
class WheelToScroll : public QObject
{
public:
    WheelToScroll(QObject* parent = 0) : QObject(parent), m_surface(0) {}

    // The webOS surface. Only wheels aimed at it are acted on.
    void watch(QWidget* surface) { m_surface = surface; }

protected:
    virtual bool eventFilter(QObject* o, QEvent* e);

private:
    QWidget* m_surface;
};

#endif /* WHEELTOSCROLL_H */
