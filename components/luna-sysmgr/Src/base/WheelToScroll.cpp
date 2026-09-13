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

#include "WheelToScroll.h"

#include <QWheelEvent>
#include <QWidget>

#include "Event.h"
#include "SystemUiController.h"
#include "Time.h"
#include "WebAppMgrProxy.h"
#include "Window.h"
#include "WindowServer.h"

#include <webos_wheel.h>

bool WheelToScroll::eventFilter(QObject* o, QEvent* e)
{
    if (e->type() != QEvent::Wheel)
        return QObject::eventFilter(o, e);

    if (m_surface && o != m_surface)
        return QObject::eventFilter(o, e);

    // The card the user is looking at. webOS shows one at a time, so there is
    // no hit-testing to do beyond checking the pointer is actually over it --
    // a wheel turned above the status bar or beside a minimised card should do
    // nothing rather than scroll something out of sight.
    Window* win = SystemUiController::instance()->activeCardWindow();
    if (!win)
        return QObject::eventFilter(o, e);

    QWheelEvent* wheel = static_cast<QWheelEvent*>(e);

    // Viewport -> scene -> the card's own coordinates. The last step is what
    // CardWindow::mapCoordinates does for touches; it is protected, and it is
    // exactly a shift by the bounding rect's origin, which boundingRect() is
    // public enough to give us. Reproducing two subtractions is not the kind of
    // duplication that drifts.
    const QPointF scenePos =
        WindowServer::instance()->mapToScene(wheel->position().toPoint());
    const QPointF local = win->mapFromScene(scenePos);
    const QRectF bounds = win->boundingRect();
    if (!bounds.contains(local))
        return QObject::eventFilter(o, e);

    WebosWheel::Scroll scroll;
    scroll.x = (int) (local.x() - bounds.x());
    scroll.y = (int) (local.y() - bounds.y());
    scroll.angleX = wheel->angleDelta().x();
    scroll.angleY = wheel->angleDelta().y();
    scroll.pixelX = wheel->pixelDelta().x();
    scroll.pixelY = wheel->pixelDelta().y();

    Event ev;
    WebosWheel::pack(ev, scroll, Time::curSysTimeMs());

    // Straight to the proxy rather than through the card: CardWindow::inputEvent
    // is the same call one line further on, and going around it keeps this out
    // of HP's class entirely. EventThrottler, which the proxy consults, only
    // inspects pen and gesture events, so nothing here is dropped.
    WebAppMgrProxy::instance()->inputEvent(win, &ev);
    return true;
}
