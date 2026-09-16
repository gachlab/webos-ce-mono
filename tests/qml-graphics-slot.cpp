// QmlGraphicsSlot's contract: a native graphics item shown where a hosted QML
// scene keeps its place.
//
// The dashboard menu is the case. Its QML said
//
//     mainMenuItem.children: [DashboardContainer]
//
// which QtQuick 2 refuses ("Cannot append DashboardWindowContainer to a QML
// list"), so the notifications never showed. The slot puts the container in
// the host's graphics subtree and follows the QML item instead.
//
// What has to hold:
//
//   1. the native item sits where the place is, and moves when it moves
//   2. it is clipped by the QML items around the place that clip
//   3. it takes the opacity and visibility the QML scene bakes into its image
//   4. a touch on it arrives as mouse events, even when the item itself
//      accepts touch and drops it, as HP's dashboard container does (found
//      live: every swipe on a notification was swallowed)
//   5. a touch beside it still reaches the QML
//
// Verified by mutation: leaving the item its touch, not accepting touch in the
// slot, dropping the clip, the opacity, the visibility, the move, the narrow
// shape, or QmlSceneItem's signalRendered each turn this red.
//
//   ./qml-graphics-slot -platform offscreen
#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTest>
#include <cstdio>

#include "QmlGraphicsSlot.h"
#include "QmlSceneItem.h"

static const char* kQml = R"QML(
import QtQuick 2.0
Item {
    width: 200; height: 160
    property alias place: place
    property alias frame: frame
    property bool tapped: false
    MouseArea { anchors.fill: parent; onPressed: parent.tapped = true }
    Item {
        id: frame
        x: 20; y: 30; width: 100; height: 50
        clip: true
        Item { id: place; x: 10; y: -20; width: 150; height: 80 }
    }
}
)QML";

// Shaped like HP's DashboardWindowContainer in menu mode: it asks for touch
// and swallows it, and does its work in the mouse handlers.
class Native : public QGraphicsObject
{
public:
    Native() { setAcceptTouchEvents(true); }

    QRectF boundingRect() const override { return QRectF(0, 0, 150, 80); }
    void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override {}

    QList<QEvent::Type> mouse;
    QPointF pressedAt;
    QPointF lastMoveDelta;

protected:
    bool sceneEvent(QEvent* event) override
    {
        if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate
            || event->type() == QEvent::TouchEnd)
            return true;
        return QGraphicsObject::sceneEvent(event);
    }
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override
    {
        mouse << e->type();
        pressedAt = e->pos();
        e->accept();
    }
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override
    {
        mouse << e->type();
        lastMoveDelta = e->pos() - e->buttonDownPos(Qt::LeftButton);
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override { mouse << e->type(); }
};

static int failures = 0;

static void check(const char* what, bool ok, const QString& detail = QString())
{
    if (!ok)
        ++failures;
    std::printf("%-52s %s %s\n", what, ok ? "ok" : "FAILED", qPrintable(detail));
}

static QString pt(const QPointF& p)
{
    return QString("(%1,%2)").arg(p.x()).arg(p.y());
}

int main(int argc, char** argv)
{
    QmlSceneItem::setUpSoftwareBackend();
    QApplication app(argc, argv);

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(kQml, QUrl());
    if (component.isError()) {
        std::printf("the test's QML does not load: %s\n", qPrintable(component.errorString()));
        return 1;
    }

    QGraphicsScene scene(0, 0, 300, 300);
    QGraphicsView view(&scene);
    view.viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    view.setFrameStyle(0);
    view.resize(300, 300);
    view.show();

    QmlSceneItem* host = new QmlSceneItem(&component);
    scene.addItem(host);
    host->setPos(40, 40);
    QQuickItem* root = host->rootItem();
    if (!root) {
        std::printf("no QML root\n");
        return 1;
    }
    QQuickItem* place = root->property("place").value<QQuickItem*>();
    QQuickItem* frame = root->property("frame").value<QQuickItem*>();

    Native* native = new Native;
    QmlGraphicsSlot* slot = new QmlGraphicsSlot(host, place, native);
    QCoreApplication::processEvents();

    // 1. where the place is, in the host's coordinates
    check("the item sits on its place", native->pos() == QPointF(30, 10), pt(native->pos()));
    frame->setX(25);
    QTest::qWait(50);
    check("and follows it when the QML moves", native->pos() == QPointF(35, 10), pt(native->pos()));

    // 2. clipped to the clipping frame around the place
    const QRectF clip = slot->boundingRect();
    check("clipped by the QML around it", clip == QRectF(25, 30, 100, 50),
          QString("%1,%2 %3x%4").arg(clip.x()).arg(clip.y()).arg(clip.width()).arg(clip.height()));
    check("and the scene clips it there",
          slot->flags().testFlag(QGraphicsItem::ItemClipsChildrenToShape));

    // 3. what the QML scene bakes into its image
    frame->setOpacity(0.5);
    QTest::qWait(50);
    check("takes the opacity of the items above the place", qFuzzyCompare(slot->opacity(), 0.5),
          QString::number(slot->opacity()));
    frame->setOpacity(1.0);
    place->setVisible(false);
    QTest::qWait(50);
    check("hides when the place is hidden", !slot->isVisible());
    place->setVisible(true);
    QTest::qWait(50);
    check("and shows again", slot->isVisible());

    // 4. a touch on the item, as the shell delivers it
    QPointingDevice* device = QTest::createTouchDevice();
    const QPoint on = view.mapFromScene(host->mapToScene(QPointF(60, 50)));
    QTest::touchEvent(view.viewport(), device).press(1, on);
    QTest::touchEvent(view.viewport(), device).move(1, on + QPoint(30, 0));
    QTest::touchEvent(view.viewport(), device).release(1, on + QPoint(30, 0));
    QCoreApplication::processEvents();

    const QList<QEvent::Type> wanted = {QEvent::GraphicsSceneMousePress,
                                        QEvent::GraphicsSceneMouseMove,
                                        QEvent::GraphicsSceneMouseRelease};
    check("a touch on the item arrives as press, move, release", native->mouse == wanted,
          QString("%1 events").arg(native->mouse.size()));
    check("at the item's own coordinates", native->pressedAt == QPointF(25, 40), pt(native->pressedAt));
    check("with the drag measured from the press", native->lastMoveDelta == QPointF(30, 0),
          pt(native->lastMoveDelta));
    check("and the QML under it does not see it", !root->property("tapped").toBool());

    // 5. beside the item but inside the clip, where the slot's rect is and the
    // item is not: the QML's
    const QPoint beside = view.mapFromScene(host->mapToScene(QPointF(30, 50)));
    QTest::touchEvent(view.viewport(), device).press(1, beside);
    QTest::touchEvent(view.viewport(), device).release(1, beside);
    QCoreApplication::processEvents();
    check("a touch beside it still reaches the QML", root->property("tapped").toBool());

    return failures == 0 ? 0 : 1;
}
