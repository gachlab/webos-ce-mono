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

#include "HoverDelivery.h"

#include <QMouseEvent>
#include <QPointF>

#include "SysMgrWebBridge.h"

#include <webos_hover.h>

void HoverDelivery::deliver(SysMgrWebBridge* bridge, const SysMgrEvent& event)
{
    if (!bridge || !bridge->page())
        return;

    const WebosHover::Hover hover = WebosHover::unpack(event);
    const QPointF where(hover.x, hover.y);

    // On the stack: sendEvent does not take ownership. NoButton in both the
    // button and the buttons argument is what makes this a hover rather than a
    // drag -- deliverToEmbedded reads the latter to decide whether it is
    // scrolling, and a page reads it to decide whether the pointer is pressed.
    QMouseEvent move(QEvent::MouseMove, where, where,
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);

    bridge->page()->event(&move);
}
