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

#include "HoverToMouseMove.h"

#include <QMouseEvent>
#include <QWidget>

#include "Event.h"
#include "SystemUiController.h"
#include "Time.h"
#include "WebAppMgrProxy.h"
#include "Window.h"
#include "WindowServer.h"

#include <webos_hover.h>

namespace {
// One hover per frame at 60Hz. Higher costs IPC for moves no page can react to
// faster than it paints; lower makes the pointer feel like it is dragging
// behind the cursor.
const unsigned int kMinIntervalMs = 16;
}

bool HoverToMouseMove::eventFilter(QObject* o, QEvent* e)
{
    if (e->type() != QEvent::MouseMove)
        return QObject::eventFilter(o, e);

    if (m_surface && o != m_surface)
        return QObject::eventFilter(o, e);

    QMouseEvent* move = static_cast<QMouseEvent*>(e);

    // A hover is a move with nothing held. With a button down this is a drag,
    // which already reaches the page as pen events and is what enyo's own
    // dragging is built on -- sending a hover alongside it would give the page
    // two stories about the same gesture.
    if (move->buttons() != Qt::NoButton)
        return QObject::eventFilter(o, e);

    Window* win = SystemUiController::instance()->activeCardWindow();
    if (!win)
        return QObject::eventFilter(o, e);

    const QPointF scenePos =
        WindowServer::instance()->mapToScene(move->position().toPoint());
    const QPointF local = win->mapFromScene(scenePos);
    const QRectF bounds = win->boundingRect();
    if (!bounds.contains(local))
        return QObject::eventFilter(o, e);

    WebosHover::Hover hover;
    hover.x = (int) (local.x() - bounds.x());
    hover.y = (int) (local.y() - bounds.y());

    const unsigned int now = Time::curSysTimeMs();
    if (hover.x == m_lastX && hover.y == m_lastY)
        return QObject::eventFilter(o, e);
    if (m_lastSent != 0 && (now - m_lastSent) < kMinIntervalMs)
        return QObject::eventFilter(o, e);
    m_lastX = hover.x;
    m_lastY = hover.y;
    m_lastSent = now;

    Event ev;
    WebosHover::pack(ev, hover, now);
    WebAppMgrProxy::instance()->inputEvent(win, &ev);

    // Deliberately not consumed. MouseEventEater runs after this and still
    // ignores the move, so Qt's touch synthesis behaves exactly as it did.
    return QObject::eventFilter(o, e);
}
