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

#include "StripDragToCoreNavi.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QWidget>

#include "SysMgrDeviceKeydefs.h"
#include "WindowServer.h"

#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
    #define KEYS Qt
#else
    #define KEYS
#endif

namespace {

// GestureStrip lives in HostQtDesktop.cpp with no header of its own, so there
// is no type to cast to. It declares Q_OBJECT, which is what makes it
// nameable from here.
const char kStripClassName[] = "GestureStrip";

// Both halves, exactly as GestureStrip::postGesture does it. Sending only the
// press would be silent: SystemUiController acts on these in its KeyRelease
// branch -- Key_CoreNavi_Launcher at line 441 -- so a press alone reaches
// nothing.
void postBoth(int key)
{
    QWidget* window = WindowServer::instance();
    if (!window)
        return;

    // The same line GestureStrip::postGesture prints, and for the same reason:
    // without it the only evidence that this fired is whether the screen moved.
    // That cost a whole round of "did it work?" while this was being built --
    // HP's horizontal gestures could be counted in the log and ours could not.
    g_warning("STRIP: postGesture key=%d destino=%p", key, (void*) window);

    QApplication::postEvent(window, new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier));
    QApplication::postEvent(window, new QKeyEvent(QEvent::KeyRelease, key, Qt::NoModifier));
}

} // namespace

bool StripDragToCoreNavi::eventFilter(QObject* o, QEvent* e)
{
    const QEvent::Type type = e->type();
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonRelease)
        return QObject::eventFilter(o, e);

    if (!o || !o->metaObject()
        || qstrcmp(o->metaObject()->className(), kStripClassName) != 0)
        return QObject::eventFilter(o, e);

    QMouseEvent* mouse = static_cast<QMouseEvent*>(e);
    if (mouse->button() != Qt::LeftButton)
        return QObject::eventFilter(o, e);

    if (type == QEvent::MouseButtonPress) {
        m_tracking = true;
        m_downPos = mouse->position().toPoint();
        return QObject::eventFilter(o, e);
    }

    if (!m_tracking)
        return QObject::eventFilter(o, e);
    m_tracking = false;

    switch (decide(m_downPos, mouse->position().toPoint())) {
    case Up:
        postBoth(KEYS::Key_CoreNavi_Launcher);
        break;
    case Down:
        postBoth(KEYS::Key_CoreNavi_SwipeDown);
        break;
    case None:
        break;
    }

    // Never consumed. The strip's own mouseReleaseEvent runs afterwards and
    // keeps owning the horizontal case, which already works and which
    // decide() deliberately refuses to claim.
    return QObject::eventFilter(o, e);
}
