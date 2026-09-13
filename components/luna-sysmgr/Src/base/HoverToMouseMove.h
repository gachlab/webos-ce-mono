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

#ifndef HOVERTOMOUSEMOVE_H
#define HOVERTOMOUSEMOVE_H

#include <QObject>

class QWidget;

//
// Gives a card the pointer moving over it with no button held.
//
// webOS never had a pointer, so nothing in HP's code carries a hover: a bare
// mouse move is swallowed by MouseEventEater, and Qt only synthesises touch
// while a button is down, so no touch, no pen event and nothing over the IPC.
// Web content that reveals itself on hover therefore never does -- the
// browser's player controls being the case you notice.
//
// Installed from Main.cpp beside MouseEventEater and WheelToScroll. The order
// matters and is the whole reason this works: Qt activates event filters in
// REVERSE order of installation, so this one, installed after the eater, is
// offered the move first and the eater still swallows it afterwards exactly as
// before. tests/filter-order holds that down, because if Qt ever changed it
// this would silently stop seeing anything.
//
// It does not consume the event: everything downstream of it, drags included,
// keeps the behaviour it had.
//
// No Q_OBJECT: no signals, no slots, just an override of a virtual QObject
// already has.
//
class HoverToMouseMove : public QObject
{
public:
    HoverToMouseMove(QObject* parent = 0)
        : QObject(parent), m_surface(0), m_lastX(-1), m_lastY(-1), m_lastSent(0) {}

    void watch(QWidget* surface) { m_surface = surface; }

protected:
    virtual bool eventFilter(QObject* o, QEvent* e);

private:
    QWidget* m_surface;
    // A pointer produces hundreds of moves a second and each one here would be
    // an IPC message and a synthesised QMouseEvent inside WebAppMgr. These hold
    // it to one per frame, and drop the ones that did not actually move.
    int m_lastX;
    int m_lastY;
    unsigned int m_lastSent;
};

#endif /* HOVERTOMOUSEMOVE_H */
