#include "qtwebkit_compat.h"

#include <QCoreApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QWebEngineView>

void QWebPage::sendKeyToHostPage(QKeyEvent* event)
{
    // Deliberately none of what event() does: no embedded delivery, and no
    // keyboard owner. This page's own widget, always.
    QWidget* target = m_view->focusProxy() ? m_view->focusProxy() : m_view;
    if (target)
        QCoreApplication::sendEvent(target, event);
}

bool QWebPage::event(QEvent* event)
{
    if (deliverToEmbedded(event))
        return true;

    // Typing goes to whatever was pressed last. WebAppMgr sends every key to
    // the host page, so an embedded page would never see one otherwise.
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::InputMethod:
        if (!m_keyboardOwner.isNull()) {
            QWidget* target = m_keyboardOwner->m_view->focusProxy()
                            ? m_keyboardOwner->m_view->focusProxy()
                            : m_keyboardOwner->m_view;
            return QCoreApplication::sendEvent(target, event);
        }
        break;
    default:
        break;
    }

    switch (event->type()) {
    // WebAppMgr's "your window was activated" (WindowedWebApp::focusedEvent).
    // Dropped, no page ever had the focus: document.hasFocus() was false in
    // every card, and a field focused by script never heard its focus event --
    // which is where Just Type clears its "Just type..." hint.
    case QEvent::FocusIn:
    case QEvent::FocusOut: {
        QWidget* target = m_view->focusProxy() ? m_view->focusProxy() : m_view;
        m_focusedWidget = event->type() == QEvent::FocusIn ? target : nullptr;
        return QCoreApplication::sendEvent(target, event);
    }
    // Input goes to a window that has the focus, and the page may not have it:
    // the window was never told it was activated, or was told before its page
    // loaded -- the launcher is, at startup -- and the widget that heard it is
    // not the one rendering now. Just Type's field went on showing its hint
    // with the typing in front of it ("ASDASDAJust type..."). So a page that is
    // typed into, pressed or touched takes the focus first, as a real window
    // would.
    case QEvent::MouseButtonPress:
    case QEvent::KeyPress:
    case QEvent::InputMethod:
    case QEvent::TouchBegin:
        if (m_focusedWidget != (m_view->focusProxy() ? m_view->focusProxy() : m_view)) {
            QFocusEvent in(QEvent::FocusIn, Qt::OtherFocusReason);
            QWebPage::event(&in);
        }
        Q_FALLTHROUGH();
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::Wheel:
    case QEvent::KeyRelease:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel: {
        QWidget* target = m_view->focusProxy() ? m_view->focusProxy() : m_view;
        return QCoreApplication::sendEvent(target, event);
    }
    default:
        return QObject::event(event);
    }
}
