// A touch a container keeps reaches the window under the finger as mouse.
//
// HP's GraphicsItemContainer accepts touch and drops it. On a device the same
// press also came as a mouse event, which is what the windows inside handle;
// here it did not, and the location alert's buttons (#10) took no taps at all.
// The container now hands the touch to the child under the finger.
//
// Verified by mutation: without picking the topmost child under the finger,
// with an item outside the container picked, or without keeping the pressed
// child for the rest of the touch, this turns red.

#include <QApplication>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QTest>
#include <cstdio>

#include "TouchAsMouse.h"

// Shaped like HP's container: takes touch, keeps it.
struct Container : QGraphicsRectItem {
    Container() : QGraphicsRectItem(0, 0, 200, 200) { setAcceptTouchEvents(true); }
    bool sceneEvent(QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
            TouchAsMouse::sendToChild(event, this);
            return true;
        default:
            return QGraphicsRectItem::sceneEvent(event);
        }
    }
};

// Shaped like an AlertWindow: reads mouse.
struct Window : QGraphicsRectItem {
    explicit Window(const QRectF& rect, QGraphicsItem* parent) : QGraphicsRectItem(rect, parent) {}
    QList<QEvent::Type> mouse;
    QPointF pressedAt;
    QPointF releasedAt;
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override
    {
        mouse << e->type();
        pressedAt = e->pos();
    }
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override { mouse << e->type(); }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override
    {
        mouse << e->type();
        releasedAt = e->pos();
    }
};

static int failures = 0;

static void check(const char* what, bool ok, const QString& detail = QString())
{
    if (!ok)
        ++failures;
    std::printf("%-56s %s %s\n", what, ok ? "ok" : "FAILED", qPrintable(detail));
}

static QString pt(const QPointF& p)
{
    return QString("(%1,%2)").arg(p.x()).arg(p.y());
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QGraphicsScene scene(0, 0, 300, 300);
    QGraphicsView view(&scene);
    view.viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    view.setFrameStyle(0);
    view.resize(300, 300);
    view.show();

    Container* container = new Container;
    scene.addItem(container);
    container->setPos(50, 50);
    // Two windows, the second on top of the first where they overlap.
    Window* lower = new Window(QRectF(0, 0, 100, 100), container);
    Window* upper = new Window(QRectF(0, 0, 100, 100), container);
    upper->setPos(60, 60);
    // Something of the shell's above the container, outside it, that does not
    // take touch: the touch is the container's, and not this item's.
    Window* outside = new Window(QRectF(0, 0, 50, 50), nullptr);
    scene.addItem(outside);
    outside->setPos(210, 60);
    outside->setZValue(10);

    QPointingDevice* device = QTest::createTouchDevice();
    const auto at = [&](qreal x, qreal y) { return view.mapFromScene(container->mapToScene(QPointF(x, y))); };

    // A tap on the lower window alone.
    QTest::touchEvent(view.viewport(), device).press(1, at(20, 30));
    QTest::touchEvent(view.viewport(), device).release(1, at(20, 30));
    QCoreApplication::processEvents();
    const QList<QEvent::Type> tap = {QEvent::GraphicsSceneMousePress, QEvent::GraphicsSceneMouseRelease};
    check("a tap reaches the window under it as press and release", lower->mouse == tap,
          QString("%1 events").arg(lower->mouse.size()));
    check("at the window's own coordinates", lower->pressedAt == QPointF(20, 30), pt(lower->pressedAt));
    check("and the other window hears nothing", upper->mouse.isEmpty());

    // Where they overlap, the one on top; a drag off it stays with it.
    lower->mouse.clear();
    QTest::touchEvent(view.viewport(), device).press(1, at(70, 70));
    QTest::touchEvent(view.viewport(), device).move(1, at(10, 10));
    QTest::touchEvent(view.viewport(), device).release(1, at(10, 10));
    QCoreApplication::processEvents();
    const QList<QEvent::Type> drag = {QEvent::GraphicsSceneMousePress, QEvent::GraphicsSceneMouseMove,
                                      QEvent::GraphicsSceneMouseRelease};
    check("where windows overlap, the top one takes it", upper->mouse == drag,
          QString("%1 events").arg(upper->mouse.size()));
    check("and keeps it when the finger leaves it", upper->releasedAt == QPointF(-50, -50), pt(upper->releasedAt));
    check("the one below hears nothing", lower->mouse.isEmpty(), QString("%1 events").arg(lower->mouse.size()));

    // Nothing under the finger: nothing sent.
    upper->mouse.clear();
    QTest::touchEvent(view.viewport(), device).press(1, at(180, 20));
    QTest::touchEvent(view.viewport(), device).release(1, at(180, 20));
    QCoreApplication::processEvents();
    check("a touch on the container's own background reaches no window",
          lower->mouse.isEmpty() && upper->mouse.isEmpty());
    check("not even one above the container but outside it", outside->mouse.isEmpty(),
          QString("%1 events").arg(outside->mouse.size()));

    return failures == 0 ? 0 : 1;
}
