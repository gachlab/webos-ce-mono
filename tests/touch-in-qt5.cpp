// Test of the Qt5 input path, without bringing webOS up.
//
// webOS only registers a finger when a QEvent::TouchBegin reaches a
// QGraphicsItem (Page::sceneEvent -> touchStartEvent). Since Qt5 only
// synthesizes touches from mouse events nobody accepted -- and QGraphicsView
// accepts the press -- WindowServer builds them by hand. This checks, in
// isolation, whether that hand-built event actually reaches the item.
//
// Runs headless:  ./touch-in-qt5 -platform offscreen
#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsObject>
#include <QTouchEvent>
#include <QMouseEvent>
#include <cstdio>

static int seen[3] = {0, 0, 0};   // begin, update, end

class SpyItem : public QGraphicsObject {
public:
    SpyItem() { setAcceptTouchEvents(true); }
    QRectF boundingRect() const override { return QRectF(0, 0, 400, 400); }
    void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override {}
protected:
    bool sceneEvent(QEvent* e) override {
        switch (e->type()) {
        case QEvent::TouchBegin:  seen[0]++; e->accept(); return true;
        case QEvent::TouchUpdate: seen[1]++; e->accept(); return true;
        case QEvent::TouchEnd:    seen[2]++; e->accept(); return true;
        default: break;
        }
        return QGraphicsObject::sceneEvent(e);
    }
};

// The same translation WindowServer::deliverAsTouch performs.
static QTouchDevice* touchDevice()
{
    static QTouchDevice* d = 0;
    if (!d) {
        d = new QTouchDevice;
        d->setType(QTouchDevice::TouchScreen);
        d->setCapabilities(QTouchDevice::Position);
    }
    return d;
}

static bool sendTouch(QGraphicsView* view, QEvent::Type type,
                        Qt::TouchPointState state, const QPointF& p)
{
    QTouchEvent::TouchPoint point(0);
    point.setState(state);
    point.setPos(p);
    point.setScenePos(p);
    point.setScreenPos(p);
    point.setLastPos(p);  point.setLastScenePos(p);  point.setLastScreenPos(p);
    point.setStartPos(p); point.setStartScenePos(p); point.setStartScreenPos(p);
    point.setPressure(state == Qt::TouchPointReleased ? 0.0 : 1.0);

    QList<QTouchEvent::TouchPoint> points;
    points.append(point);

    QTouchEvent touch(type, touchDevice(), Qt::NoModifier, state, points);
    touch.setAccepted(false);
    QApplication::sendEvent(view->viewport(), &touch);
    return touch.isAccepted();
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QGraphicsScene scene(0, 0, 400, 400);
    SpyItem* item = new SpyItem;
    scene.addItem(item);

    QGraphicsView view(&scene);
    view.viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    view.resize(400, 400);
    view.show();

    const QPointF p(100, 100);
    bool okBegin  = sendTouch(&view, QEvent::TouchBegin,  Qt::TouchPointPressed,  p);
    bool okUpdate = sendTouch(&view, QEvent::TouchUpdate, Qt::TouchPointMoved,    p);
    bool okEnd    = sendTouch(&view, QEvent::TouchEnd,    Qt::TouchPointReleased, p);

    printf("TouchBegin  -> item:%d  accepted:%d\n", seen[0], okBegin);
    printf("TouchUpdate -> item:%d  accepted:%d\n", seen[1], okUpdate);
    printf("TouchEnd    -> item:%d  accepted:%d\n", seen[2], okEnd);

    bool ok = seen[0] == 1 && seen[1] == 1 && seen[2] == 1;
    printf("%s\n", ok ? "OK: the hand-built touch reaches the item"
                        : "FAIL: the item does not receive the touch");
    return ok ? 0 : 1;
}
