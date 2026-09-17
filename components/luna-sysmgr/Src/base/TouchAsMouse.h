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

#ifndef TOUCHASMOUSE_H
#define TOUCHASMOUSE_H

class QEvent;
class QGraphicsItem;

//
// A touch, handed on as the mouse event HP's native items handle.
//
// The shell's input is touch (see WindowServer::deliverAsTouch). On a device a
// touch nobody took also came as a mouse event, and that is what HP's windows
// read: an alert's buttons, a dashboard's notifications. Here an item that
// accepts touch keeps it, so no mouse event follows; these hand it on.
//
namespace TouchAsMouse {

// Sends `event`, a touch, to `target` as a mouse press, move or release of the
// first finger. False when it is not a touch, or cannot be sent.
bool send(QEvent* event, QGraphicsItem* target);

// The same, to the item under the finger inside `container`, as QGraphicsScene
// would pick it for a mouse press: the topmost descendant that takes the left
// button. The item a press went to gets the rest of that touch.
bool sendToChild(QEvent* event, QGraphicsItem* container);

}

#endif /* TOUCHASMOUSE_H */
