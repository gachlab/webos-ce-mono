/* @@@LICENSE
*
*      Copyright (c) 2010-2013 Hewlett-Packard Development Company, L.P.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */




#ifndef MOUSEEVENTEATER_H
#define MOUSEEVENTEATER_H

#include <QObject>
#include <QWidget>
#include <QEvent>

QT_BEGIN_NAMESPACE
class QEvent;
QT_END_NAMESPACE

class MouseEventEater : public QObject
{
    Q_OBJECT

public:
    MouseEventEater(QObject *parent = 0) : QObject(parent), m_surface(0) {}

    // Only eats mouse events aimed at the webOS surface. It used to eat them
    // for EVERY object, because the filter is installed on the QCoreApplication:
    // that left the gesture strip's home button (a plain QPushButton) unable to
    // ever receive a click. HP's Qt4 build does not have this filter at all --
    // it is Qt5 port code -- which is why the button works there.
    // Verified in tests/eater-synthesis-qt5.cpp.
    void watch(QWidget *surface) { m_surface = surface; }

protected:
    virtual bool eventFilter(QObject *o, QEvent *e) {
        if (e->type() == QEvent::MouseButtonRelease ||
            e->type() == QEvent::MouseButtonPress ||
            e->type() == QEvent::MouseButtonDblClick ||
            e->type() == QEvent::MouseMove) {
            if (m_surface && o != m_surface)
                return QObject::eventFilter(o, e);
            e->ignore();
            return true;
        }

        return QObject::eventFilter(o, e);
    }

private:
    QWidget *m_surface;
};

#endif /* MOUSEEVENTEATER_H */
