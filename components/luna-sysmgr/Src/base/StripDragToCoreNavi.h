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

#ifndef STRIPDRAGTOCORENAVI_H
#define STRIPDRAGTOCORENAVI_H

#include <QObject>
#include <QPoint>

class QWidget;

//
// The half of webOS's gesture area that never fires on a desktop.
//
// The strip below the screen -- HostQtDesktop's GestureStrip, the band with
// Lock, the home button and Keyboard -- is HP's stand-in for the gesture area a
// Pre had under its display. Dragging sideways on it works: its
// mouseReleaseEvent compares the two deltas and posts Next, Menu, Previous or
// Back. Dragging UP or DOWN does nothing at all, and the reason is visible in
// that same function:
//
//     if ((deltaX*deltaX + deltaY*deltaY) > 100) {
//         if (deltaX*deltaX > deltaY*deltaY) {   // horizontal only
//             ...
//         }
//         // nothing here for the vertical case
//     }
//
// HP delegated the vertical gesture to flickGesture(), which runs only from a
// SysMgrGestureFlick. That gesture is produced by FlickGestureRecognizer, which
// reads TOUCH events -- and Qt synthesises touch only from mouse events nobody
// handled. GestureStrip overrides the mouse handlers, so its events are handled,
// so no touch is ever synthesised, so the flick is never recognised. The path is
// dead by construction on this port.
//
// MEASURED in a running session, driving the strip by hand: Key_CoreNavi_Back
// was posted 7 times and Key_CoreNavi_Menu once, while Key_CoreNavi_Launcher
// (Key_End, 0xE0B2) was posted ZERO times. Sideways works and up does not, which
// is exactly what the missing branch predicts.
//
// This restores it from outside. GestureStrip declares Q_OBJECT, so an
// application-wide filter can recognise it by class name without needing a
// pointer to it, and nothing in HP's sources changes.
//
// The decision itself is deliberately separate and pure, so it can be tested
// without a shell: see tests/strip-drag.
//
class StripDragToCoreNavi : public QObject
{
public:
    StripDragToCoreNavi(QObject* parent = 0) : QObject(parent), m_tracking(false) {}

    // What a drag between two points means. Kept free of Qt widgets and of
    // webOS headers so a test can call it directly.
    enum Direction {
        None = 0,   // too short, or horizontal -- HP's own code owns that case
        Up,         // let go of the card: Key_CoreNavi_Launcher
        Down        // come back into it: Key_CoreNavi_SwipeDown
    };

    // The thresholds are HP's, taken from the horizontal branch that works: a
    // squared distance over 100, and dominance decided by comparing squares.
    // Using the same numbers means a drag that HP would have called horizontal
    // is still horizontal here, and only what he left unhandled is claimed.
    static Direction decide(const QPoint& down, const QPoint& up)
    {
        const int dx = up.x() - down.x();
        const int dy = up.y() - down.y();

        if ((dx * dx + dy * dy) <= 100)
            return None;
        if (dx * dx >= dy * dy)
            return None;

        return dy < 0 ? Up : Down;
    }

protected:
    virtual bool eventFilter(QObject* o, QEvent* e);

private:
    bool m_tracking;
    QPoint m_downPos;
};

#endif /* STRIPDRAGTOCORENAVI_H */
