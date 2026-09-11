// What is in a touch point's pos() depends on how the event got there.
//
// Every Qt 5 touch handler in the shell starts by reading a position out of the
// event, and whether it then has to convert it depends entirely on the delivery
// path. Get that wrong and the code still compiles and runs, pointing at a
// plausible spot in the wrong place -- which is how the card-dismiss gesture was
// dead, and how a first reading of LockWindow sent me the wrong way.
//
// There are two paths in this shell:
//
//   1. QGraphicsScene delivers the event to the item (Item::sceneEvent).
//      The scene rewrites each touch point for that item first, so pos() is
//      ITEM-LOCAL and scenePos() is the scene position.
//
//   2. WindowServer's filter chain hands the raw viewport event to a window
//      manager, which passes it on (TopLevelWindowManager::handleEvent ->
//      LockWindow::handleFilteredSceneEvent). Nothing rewrote anything, so
//      pos() is whatever the view put in it and is NOT item-local.
//
// This pins both down with an item deliberately placed away from the origin and
// centred on itself, the way the shell's full-screen items are, so a missing or
// doubled conversion cannot hide behind a zero offset.
//
//   ./touch-coordinate-spaces-qt5 -platform offscreen
#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsObject>
#include <QTouchEvent>
#include <cstdio>

static int failures = 0;

static void check(const char *what, const QPointF &got, const QPointF &want)
{
    const bool ok = int(got.x()) == int(want.x()) && int(got.y()) == int(want.y());
    printf("  %-46s (%g,%g)%s\n", what, got.x(), got.y(),
           ok ? "" : QString(" <- expected (%1,%2)").arg(want.x()).arg(want.y()).toUtf8().constData());
    if (!ok)
        ++failures;
}

// Centred on its own origin and parented at the middle of the scene, like
// LockWindow, CardWindowManager and the other full-screen items.
class CentredItem : public QGraphicsObject
{
public:
    CentredItem(qreal w, qreal h) : m_bounds(-w / 2, -h / 2, w, h)
    {
        setAcceptTouchEvents(true);
    }
    QRectF boundingRect() const override { return m_bounds; }
    void paint(QPainter *, const QStyleOptionGraphicsItem *, QWidget *) override {}

    // What the scene delivered, straight out of sceneEvent.
    bool sawDelivery = false;
    QPointF deliveredPos, deliveredScenePos;

protected:
    bool sceneEvent(QEvent *e) override
    {
        if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate) {
            const QTouchEvent::TouchPoint &p =
                static_cast<QTouchEvent *>(e)->touchPoints().first();
            sawDelivery = true;
            deliveredPos = p.pos();
            deliveredScenePos = p.scenePos();
            e->accept();
            return true;
        }
        return QGraphicsObject::sceneEvent(e);
    }

private:
    QRectF m_bounds;
};

#include "touch-events.h"

static QTouchEvent makeTouch(const QPointF &viewPos)
{
    QList<QTouchEvent::TouchPoint> points;
    points << TestTouch::point(0, Qt::TouchPointPressed, viewPos);
    return TestTouch::event(QEvent::TouchBegin, Qt::TouchPointPressed, points);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    const qreal W = 1024, H = 728;
    QGraphicsScene scene(0, 0, W, H);
    QGraphicsView view(&scene);
    view.setFrameStyle(0);
    view.viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    view.resize(int(W), int(H));
    view.show();

    CentredItem *item = new CentredItem(W, H);
    scene.addItem(item);
    item->setPos(W / 2, H / 2);          // centred in the scene
    printf("item at scene (%g,%g), bounds (%g,%g %gx%g)\n",
           item->pos().x(), item->pos().y(),
           item->boundingRect().x(), item->boundingRect().y(),
           item->boundingRect().width(), item->boundingRect().height());

    // A finger a long way from both origins, so no offset can cancel out.
    const QPointF touchedScenePos(700, 600);
    const QPointF expectedItemPos = touchedScenePos - QPointF(W / 2, H / 2);

    printf("\npath 1: QGraphicsScene delivers to the item\n");
    {
        QTouchEvent e = makeTouch(touchedScenePos);
        QApplication::sendEvent(view.viewport(), &e);

        if (!item->sawDelivery) {
            printf("  FAIL: the item never received the touch\n");
            ++failures;
        } else {
            check("pos() is item-local", item->deliveredPos, expectedItemPos);
            check("scenePos() is the scene position", item->deliveredScenePos, touchedScenePos);
            check("mapFromScene(scenePos()) == pos()",
                  item->mapFromScene(item->deliveredScenePos), item->deliveredPos);
        }
    }

    printf("\npath 2: the raw viewport event, handed on by a filter\n");
    {
        // What TopLevelWindowManager::handleEvent passes to LockWindow: the
        // event as the viewport saw it, untouched.
        QTouchEvent e = makeTouch(touchedScenePos);
        const QTouchEvent::TouchPoint &p = e.touchPoints().first();

        check("pos() is NOT item-local here", p.pos(), touchedScenePos);
        check("mapFromScene(pos()) is what gives item-local",
              item->mapFromScene(p.pos()), expectedItemPos);
    }

    printf("\n%s\n", failures == 0 ? "OK" : "FAIL");
    return failures == 0 ? 0 : 1;
}
