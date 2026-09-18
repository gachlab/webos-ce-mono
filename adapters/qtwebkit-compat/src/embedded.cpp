#include "detail.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QWebEngineView>

using namespace qtwebkit_compat_detail;

void QWebPage::embedPage(QWebPage* page, const QRect& rect)
{
    if (!page || page == this)
        return;

    // Already embedded: this is a move or a resize, which is what it will be
    // most of the time -- the hole travels with the page that owns it.
    for (int i = 0; i < m_embedded.size(); ++i) {
        if (m_embedded[i].page == page) {
            m_embedded[i].rect = rect;
            // An empty rect suspends the blit; it does not mean the page has
            // become nothing. Resizing its viewport to 0x0 would throw away the
            // layout it has to come back to.
            if (!rect.isEmpty())
                page->setViewportSize(rect.size());
            return;
        }
    }

    EmbeddedPage entry;
    entry.page = page;
    entry.rect = rect;
    if (!rect.isEmpty())
        page->setViewportSize(rect.size());

    // A frame of the embedded page is a frame of this one. The shell only ever
    // repaints what WindowedWebApp hands it, and that is the host page, so
    // without this the embedded page would paint into a buffer nobody asked
    // for again.
    entry.repaintLink = connect(page, &QWebPage::repaintRequested,
                                this, [this, page](const QRect&) {
        for (const EmbeddedPage& embedded : m_embedded) {
            if (embedded.page == page) {
                Q_EMIT repaintRequested(embedded.rect);
                return;
            }
        }
    });

    m_embedded.append(entry);
}

void QWebPage::setEmbeddedCutouts(QWebPage* page, const QRegion& cutouts)
{
    for (EmbeddedPage& embedded : m_embedded) {
        if (embedded.page != page)
            continue;
        if (embedded.cutouts == cutouts)
            return;
        embedded.cutouts = cutouts;
        Q_EMIT repaintRequested(embedded.rect);
        return;
    }
}

void QWebPage::removeEmbeddedPage(QWebPage* page)
{
    for (int i = m_embedded.size() - 1; i >= 0; --i) {
        if (m_embedded[i].page != page && !m_embedded[i].page.isNull())
            continue;
        disconnect(m_embedded[i].repaintLink);
        m_embedded.removeAt(i);
    }
}

// coordinates and gives them to the host page (WindowedWebApp.cpp:405), which
// knows only its own widget -- so with a page embedded in it, the browser drew
// its content and nothing in it could be clicked or scrolled: every touch was
// delivered to the page holding the hole, where there is only an empty div.
//
// Keyboard events are deliberately not routed here. They follow focus rather
// than a position, and sending them to an embedded page would take typing away
// from the address bar, which is the app's own field. That needs a focus model
// of its own.
bool QWebPage::deliverToEmbedded(QEvent* event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::Wheel:
        break;
    default:
        return false;
    }

    const QPointF where = static_cast<QSinglePointEvent*>(event)->position();

    // Last registered is topmost, so it is asked first.
    for (int i = m_embedded.size() - 1; i >= 0; --i) {
        const EmbeddedPage& embedded = m_embedded[i];
        if (embedded.page.isNull() || embedded.rect.isEmpty())
            continue;
        if (!embedded.rect.contains(where.toPoint()) || embedded.cutouts.contains(where.toPoint()))
            continue;

        const QPointF local = where - QPointF(embedded.rect.topLeft());
        QWidget* target = embedded.page->m_view->focusProxy()
                        ? embedded.page->m_view->focusProxy()
                        : embedded.page->m_view;

        bool handled = false;
        if (event->type() == QEvent::Wheel) {
            QWheelEvent* wheel = static_cast<QWheelEvent*>(event);
            QWheelEvent translated(local, local, wheel->pixelDelta(), wheel->angleDelta(),
                                   wheel->buttons(), wheel->modifiers(),
                                   wheel->phase(), wheel->inverted());
            handled = QCoreApplication::sendEvent(target, &translated);
        } else if (event->type() == QEvent::MouseMove && m_dragging) {
            // Dragging scrolls, with the content following the finger. Nothing
            // in luna-sysmgr or webappmanager handles a wheel -- webOS scrolled
            // by gesture, and its event catalogue has no scroll member -- so
            // this is the only scrolling an embedded page can be given without
            // changing HP's input path. The cost is that a drag no longer
            // selects text there, or drags a scrollbar: same gesture, and on a
            // touchscreen it belongs to scrolling.
            //
            // tests/embedded-scroll checks that a drag scrolls, and deliberately
            // does not check how far. An earlier version asserted 80 pixels of
            // drag should move the page 80, measured 4 in the harness, and that
            // number was used to junk this code -- which was working on the real
            // shell all along. A ten-step synthetic drag is not a real one.
            const QPointF delta = where - m_dragAt;
            m_dragAt = where;
            const QPoint pixels(int(delta.x()), int(delta.y()));
            QWheelEvent scroll(local, local, pixels, pixels,
                               Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            handled = QCoreApplication::sendEvent(target, &scroll);
        } else {
            QMouseEvent* mouse = static_cast<QMouseEvent*>(event);
            QMouseEvent translated(mouse->type(), local, local,
                                   mouse->button(), mouse->buttons(), mouse->modifiers());
            handled = QCoreApplication::sendEvent(target, &translated);
        }

        // A press is also what decides where typing goes from now on. Without
        // this the embedded widget takes Qt's focus on the first click and the
        // app's address bar can never be typed into again; with the keyboard
        // pinned to the host instead, a field inside the page could never be
        // typed into. Whichever was pressed last owns it, as in any browser.
        if (event->type() == QEvent::MouseButtonPress) {
            m_keyboardOwner = embedded.page;
            target->setFocus(Qt::MouseFocusReason);
            m_dragAt = where;
            m_dragging = true;
        } else if (event->type() == QEvent::MouseButtonRelease) {
            m_dragging = false;
        }

        return handled;
    }

    // Pressed somewhere that is not an embedded page: the host takes the
    // keyboard back, which is what makes the address bar usable again after a
    // click in the content.
    if (event->type() == QEvent::MouseButtonPress) {
        m_keyboardOwner.clear();
        if (QWidget* host = m_view->focusProxy() ? m_view->focusProxy() : m_view)
            host->setFocus(Qt::MouseFocusReason);
    }

    return false;
}
