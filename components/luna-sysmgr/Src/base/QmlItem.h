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

#ifndef QMLITEM_H
#define QMLITEM_H

#include <QtGlobal>

// The type of an object instantiated from a .qml file.
//
// Under QML 1 (Qt 4) that was a QDeclarativeItem, which derives from
// QGraphicsObject, so the shell could drop it straight into its QGraphicsScene
// and call setPos()/setParentItem() on it.
//
// Under QtQuick 2 it is a QQuickItem, which is NOT a QGraphicsItem at all -- it
// renders through a scene graph owned by a QQuickWindow. The old
// qobject_cast<QGraphicsObject*>(component->create()) therefore returns null,
// silently, which is why none of the QML UI drew after the Qt 5 port.
//
// Everything the call sites do with the object other than positioning it --
// property(), setProperty(), connect(), findChild(), boundingRect() -- is
// available on both types, so a typedef covers the declarations and only the
// hosting needs real work. See QmlSceneItem, which puts a QQuickItem back into
// the QGraphicsScene.
#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
#include <QGraphicsObject>
typedef QGraphicsObject QmlItem;
#else
#include <QQuickItem>
typedef QQuickItem QmlItem;
#endif

#endif /* QMLITEM_H */
