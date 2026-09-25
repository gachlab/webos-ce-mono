#include "page_engine.h"

#include <QMouseEvent>

bool QWebPage::event(QEvent* event)
{
    // QObject parenting of m_frame during QWebPage's ctor delivers events here
    // before m_engine exists — do not touch the view yet.
    if (!m_engine)
        return QObject::event(event);
    ensureView();
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (event->type() == QEvent::MouseButtonPress
            && mouse->button() == Qt::LeftButton) {
            // Pair with the matching release; QtWebKit delivered both.
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease
            && mouse->button() == Qt::LeftButton) {
            m_engine->view->click(mouse->position().x(), mouse->position().y());
            return true;
        }
        return true;
    }
    default:
        return QObject::event(event);
    }
}
