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

#include "WheelDelivery.h"

#include <QPointF>
#include <QWheelEvent>

#include "SysMgrWebBridge.h"

#include <webos_wheel.h>

void WheelDelivery::deliver(SysMgrWebBridge* bridge, const SysMgrEvent& event)
{
    if (!bridge || !bridge->page())
        return;

    const WebosWheel::Scroll scroll = WebosWheel::unpack(event);
    const QPointF where(scroll.x, scroll.y);

    // On the stack: sendEvent does not take ownership. Both deltas are passed
    // through as they were measured -- angleDelta is what enyo reads, pixelDelta
    // is what a trackpad adds, and Chromium uses whichever it prefers.
    QWheelEvent wheel(where, where,
                      QPoint(scroll.pixelX, scroll.pixelY),
                      QPoint(scroll.angleX, scroll.angleY),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);

    bridge->page()->event(&wheel);
}
