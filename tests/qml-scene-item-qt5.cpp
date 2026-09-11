// QmlSceneItem's contract, without bringing webOS up.
//
// The shell draws through QGraphicsView. A QtQuick 2 root is a QQuickItem,
// which is not a QGraphicsItem, so it cannot be parented into that scene --
// the old qobject_cast<QGraphicsObject*> just returned null and the UI silently
// did not draw. QmlSceneItem is the seam: a QGraphicsObject hosting an
// offscreen QQuickWindow, grabbed to a QImage and blitted.
//
// Four things have to hold, and all four broke at some point while writing it:
//
//   1. the QML root comes back, as a QQuickItem
//   2. its pixels reach the QGraphicsScene
//   3. touch reaches the QML -- the shell delivers touch, not mouse, because
//      WindowServer::deliverAsTouch converts it and MouseEventEater eats the
//      rest, so a mouse-only host looks alive and answers nothing
//   4. hiding the QML root hides the host, or the host stays in the scene
//      swallowing input for a panel that is meant to be gone
//
// Verified by mutation: dropping setAcceptTouchEvents, the visibleChanged
// mirror, or the drawImage in paint() each turn this red, and reverting an
// import back to "import Qt 4.7" turns qml-loads red.
//
// NOT covered: QQuickRenderControl::initialize() has to be handed the context
// that is already current, and headless there is no current GL context, so
// passing null behaves identically here. That one only shows up with the shell's
// QGLWidget viewport in play.
//
//   ./qml-scene-item-qt5 -platform offscreen
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGuiApplication>
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTouchEvent>
#include <cstdio>

#include "QmlSceneItem.h"

static const char *kQml = R"QML(
import QtQuick 2.0
Rectangle {
    width: 120; height: 60
    color: "#ff00c8"
    property bool tapped: false
    MouseArea { anchors.fill: parent; onClicked: parent.tapped = true }
}
)QML";

#include "touch-events.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QTest>
#endif

static bool sendTouch(QGraphicsView *view, QEvent::Type type,
                      Qt::TouchPointState state, const QPointF &viewPos)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Qt 6's QQuickWindow only accepts a touch point its device already
    // tracks as active, and a device only tracks the points that came in
    // through the platform. An event handed to sendEvent() never did, so it
    // was dropped with "point is not in activePoints". QTest injects through
    // QWindowSystemInterface, which is the path a real touch takes.
    Q_UNUSED(type);
    static QPointingDevice *device = QTest::createTouchDevice();
    if (state == Qt::TouchPointPressed)
        QTest::touchEvent(view->viewport(), device).press(1, viewPos.toPoint());
    else if (state == Qt::TouchPointMoved)
        QTest::touchEvent(view->viewport(), device).move(1, viewPos.toPoint());
    else
        QTest::touchEvent(view->viewport(), device).release(1, viewPos.toPoint());
    return true;
#else
    QList<QTouchEvent::TouchPoint> points;
    points << TestTouch::point(1, state, viewPos);

    QTouchEvent ev = TestTouch::event(type, state, points);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt 6 keeps the target per point and sets it during delivery.
    ev.setTarget(view->viewport());
#endif
    return QApplication::sendEvent(view->viewport(), &ev);
#endif
}

int main(int argc, char **argv)
{
    QmlSceneItem::setUpSoftwareBackend();
    QApplication app(argc, argv);

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(kQml, QUrl());
    if (component.isError()) {
        printf("FAIL: the test's own QML does not load: %s\n",
               qPrintable(component.errorString()));
        return 1;
    }

    QGraphicsScene scene(0, 0, 200, 200);
    QGraphicsView view(&scene);
    view.viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    view.setFrameStyle(0);
    view.resize(200, 200);
    view.show();

    QmlSceneItem *host = new QmlSceneItem(&component);
    scene.addItem(host);
    host->setPos(0, 0);

    // The host renders on a zero-timer, so let it run before looking.
    QCoreApplication::processEvents();

    int failures = 0;

    // 1. the root comes back
    QQuickItem *root = host->rootItem();
    printf("root item                : %s\n", root ? "present" : "MISSING");
    if (!root) {
        printf("FAIL\n");
        return 1;
    }

    // 2. the geometry follows the QML, and the pixels land in the scene
    const QRectF bounds = host->boundingRect();
    const bool sized = bounds.width() == 120 && bounds.height() == 60;
    printf("boundingRect             : %gx%g %s\n", bounds.width(), bounds.height(),
           sized ? "" : "<- expected 120x60");
    failures += sized ? 0 : 1;

    QImage shot(200, 200, QImage::Format_ARGB32);
    shot.fill(Qt::black);
    {
        QPainter p(&shot);
        scene.render(&p);
    }
    const QColor got = QColor(shot.pixel(20, 20));
    const bool painted = got == QColor("#ff00c8");
    printf("pixel in the scene       : %s %s\n", qPrintable(got.name()),
           painted ? "" : "<- expected #ff00c8");
    failures += painted ? 0 : 1;

    // 3. touch reaches the QML
    const QPointF inside(20, 20);
    sendTouch(&view, QEvent::TouchBegin, Qt::TouchPointPressed, inside);
    sendTouch(&view, QEvent::TouchEnd, Qt::TouchPointReleased, inside);
    QCoreApplication::processEvents();

    const bool tapped = root->property("tapped").toBool();
    printf("touch reached the QML    : %s\n", tapped ? "yes" : "NO");
    failures += tapped ? 0 : 1;

    // 4. hiding the root hides the host
    root->setVisible(false);
    QCoreApplication::processEvents();
    const bool hidden = !host->isVisible();
    printf("hiding root hides host   : %s\n", hidden ? "yes" : "NO");
    failures += hidden ? 0 : 1;

    root->setVisible(true);
    QCoreApplication::processEvents();
    const bool shown = host->isVisible();
    printf("showing root shows host  : %s\n", shown ? "yes" : "NO");
    failures += shown ? 0 : 1;

    printf("%s\n", failures == 0 ? "OK" : "FAIL");
    return failures == 0 ? 0 : 1;
}
