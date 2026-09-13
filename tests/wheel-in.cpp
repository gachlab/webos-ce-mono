// Test of the wheel path into a QGraphicsItem, without bringing webOS up.
//
// Nothing in luna-sysmgr or webappmanager has ever handled a wheel: webOS
// scrolled by gesture and Event::Type has no scroll member, so a wheel is
// dropped before any of HP's code sees it. Giving a card one means picking the
// event up somewhere, and there are two places to do it:
//
//   * WindowServer::viewportEvent, where the full QWheelEvent still carries
//     pixelDelta() and angleDelta(), but the target has to be hit-tested by
//     hand, or
//   * CardWindow::wheelEvent, where QGraphicsScene has already done the
//     hit-testing and handed over item-local coordinates -- but through
//     QGraphicsSceneWheelEvent, whose Qt 6 API is only delta() and
//     orientation().
//
// This is the measurement that settled it, and it argued for the first: the
// scene does deliver the wheel to the item, with item-local coordinates and a
// notch intact -- but an event carrying only pixelDelta arrives as delta 0, so
// a trackpad's high-resolution scroll is already gone by then. So the wheel is
// picked up earlier, in Src/base/WheelToScroll.cpp, where the whole
// QWheelEvent still exists.
//
// It stays a test because it is the evidence for that choice: if a future Qt
// gives QGraphicsSceneWheelEvent somewhere to put pixelDelta, the last line
// here changes and the simpler design becomes available again.
//
// Runs headless:  ./wheel-in -platform offscreen
#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsObject>
#include <QGraphicsSceneWheelEvent>
#include <QWheelEvent>
#include <cstdio>

static int   g_seen = 0;
static QPointF g_pos;
static int   g_delta = 0;

class SpyItem : public QGraphicsObject {
public:
    QRectF boundingRect() const override { return QRectF(0, 0, 400, 400); }
    void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override {}
protected:
    // QGraphicsItem::wheelEvent ignores by default, which sends the scene
    // looking for another item; accepting is what makes the card the one that
    // consumes it.
    void wheelEvent(QGraphicsSceneWheelEvent* event) override {
        g_seen++;
        g_pos = event->pos();
        g_delta = event->delta();
        event->accept();
    }
};

static void sendWheel(QGraphicsView* view, const QPointF& pos,
                      QPoint pixelDelta, QPoint angleDelta)
{
    QWheelEvent wheel(pos, view->mapToGlobal(pos.toPoint()),
                      pixelDelta, angleDelta,
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    wheel.setAccepted(false);
    QApplication::sendEvent(view->viewport(), &wheel);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QGraphicsScene scene(0, 0, 400, 400);
    SpyItem* item = new SpyItem;
    scene.addItem(item);

    QGraphicsView view(&scene);
    view.resize(400, 400);
    // No scrollbars and no frame, so view coordinates and scene coordinates
    // line up and a mismatch below means the delivery is wrong rather than the
    // viewport being offset.
    view.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view.setFrameStyle(0);
    view.show();

    // One notch of a real mouse wheel: 120 eighths of a degree, which is also
    // the unit enyo's ScrollStrategy.mousewheel reads out of wheelDeltaY.
    const QPointF where(100, 100);
    sendWheel(&view, where, QPoint(0, 0), QPoint(0, 120));

    printf("wheel at (100,100) -> item:%d  pos:(%.0f,%.0f)  delta:%d\n",
           g_seen, g_pos.x(), g_pos.y(), g_delta);

    const bool reached = g_seen == 1;
    const bool located = qFuzzyCompare(g_pos.x() + 1.0, where.x() + 1.0)
                      && qFuzzyCompare(g_pos.y() + 1.0, where.y() + 1.0);
    const bool carried = g_delta == 120;

    // A trackpad's two-finger scroll: high-resolution pixels, and on some
    // platforms no angleDelta at all. Whatever this prints is what a card can
    // ever know about that gesture through the scene, since
    // QGraphicsSceneWheelEvent has nowhere to put pixelDelta.
    g_seen = 0; g_delta = 0;
    sendWheel(&view, where, QPoint(0, 53), QPoint(0, 0));
    printf("pixelDelta-only (trackpad) -> item:%d  delta:%d%s\n",
           g_seen, g_delta,
           g_delta == 0 ? "   <- high-resolution scrolling is lost here" : "");

    const bool ok = reached && located && carried;
    printf("%s\n", ok ? "OK: the scene delivers the wheel to the item, local and intact"
                      : "FAIL: the wheel does not arrive the way CardWindow::wheelEvent needs");
    return ok ? 0 : 1;
}
