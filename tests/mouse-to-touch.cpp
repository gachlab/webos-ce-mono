// A drag has to look like a drag by the time it reaches the gesture code.
//
// webOS reads every gesture out of the differences between a touch point's
// three positions:
//
//     CardWindowManager.cpp:1410   p.scenePos() - p.startScenePos()
//     CardWindowManager.cpp:1429   p.scenePos() - p.lastScenePos()
//     page.cpp, quicklaunchbar.cpp the same three, for dragging icons
//
// The first version of the mouse-to-touch translation set all three to the
// current position, so both differences were zero on every event. Taps worked,
// and nothing else did: a card could not be thrown off screen and an icon could
// not be dragged, with nothing logged either way.
//
// This walks a press, two moves and a release through MouseToTouch and checks
// the three positions against what actually happened.
//
//   ./mouse-to-touch-qt5 -platform offscreen
#include <QGuiApplication>
#include <QMouseEvent>
#include <cstdio>

#include "MouseToTouch.h"

static int failures = 0;

static void check(const char *what, const QPointF &got, const QPointF &want)
{
    const bool ok = qFuzzyCompare(got.x() + 1.0, want.x() + 1.0)
                 && qFuzzyCompare(got.y() + 1.0, want.y() + 1.0);
    printf("  %-34s (%g,%g)%s\n", what, got.x(), got.y(),
           ok ? "" : QString(" <- expected (%1,%2)").arg(want.x()).arg(want.y()).toUtf8().constData());
    if (!ok)
        ++failures;
}

static QMouseEvent mouse(QEvent::Type type, const QPointF &at)
{
    return QMouseEvent(type, at, at, at,
                       type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                       type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                       Qt::NoModifier);
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    MouseToTouch translator;

    // A flick upwards, the gesture that closes a card.
    const QPointF down(100, 400), mid(100, 300), high(100, 160), up(100, 120);

    printf("press at (100,400)\n");
    {
        QMouseEvent e = mouse(QEvent::MouseButtonPress, down);
        const QTouchEvent::TouchPoint p = translator.translate(&e, Qt::TouchPointPressed);
        check("pos", p.scenePos(), down);
        check("startPos", p.startScenePos(), down);
        check("lastPos", p.lastScenePos(), down);
    }

    printf("move to (100,300)\n");
    {
        QMouseEvent e = mouse(QEvent::MouseMove, mid);
        const QTouchEvent::TouchPoint p = translator.translate(&e, Qt::TouchPointMoved);
        check("pos", p.scenePos(), mid);
        check("startPos  (where it went down)", p.startScenePos(), down);
        check("lastPos   (previous event)", p.lastScenePos(), down);
        check("drag so far", p.scenePos() - p.startScenePos(), QPointF(0, -100));
        check("step", p.scenePos() - p.lastScenePos(), QPointF(0, -100));
    }

    printf("move to (100,160)\n");
    {
        QMouseEvent e = mouse(QEvent::MouseMove, high);
        const QTouchEvent::TouchPoint p = translator.translate(&e, Qt::TouchPointMoved);
        check("startPos", p.startScenePos(), down);
        check("lastPos", p.lastScenePos(), mid);
        check("drag so far", p.scenePos() - p.startScenePos(), QPointF(0, -240));
        check("step", p.scenePos() - p.lastScenePos(), QPointF(0, -140));
    }

    printf("release at (100,120)\n");
    {
        QMouseEvent e = mouse(QEvent::MouseButtonRelease, up);
        const QTouchEvent::TouchPoint p = translator.translate(&e, Qt::TouchPointReleased);
        check("startPos", p.startScenePos(), down);
        check("lastPos", p.lastScenePos(), high);
        check("total flick", p.scenePos() - p.startScenePos(), QPointF(0, -280));
    }

    // A second drag must not inherit the first one's origin.
    printf("a new press elsewhere starts over\n");
    {
        const QPointF other(500, 500);
        QMouseEvent e = mouse(QEvent::MouseButtonPress, other);
        const QTouchEvent::TouchPoint p = translator.translate(&e, Qt::TouchPointPressed);
        check("startPos", p.startScenePos(), other);
        check("lastPos", p.lastScenePos(), other);
    }

    // A press with no release behind it -- a lost grab, a window change -- must
    // still start over, or the next drag is measured from a stale origin.
    printf("a press with no release before it starts over\n");
    {
        const QPointF a(200, 200), b(200, 260), c(700, 700);
        QMouseEvent p1 = mouse(QEvent::MouseButtonPress, a);
        translator.translate(&p1, Qt::TouchPointPressed);
        QMouseEvent m1 = mouse(QEvent::MouseMove, b);
        translator.translate(&m1, Qt::TouchPointMoved);

        QMouseEvent p2 = mouse(QEvent::MouseButtonPress, c);
        const QTouchEvent::TouchPoint p = translator.translate(&p2, Qt::TouchPointPressed);
        check("startPos", p.startScenePos(), c);
        check("lastPos", p.lastScenePos(), c);
        check("drag so far", p.scenePos() - p.startScenePos(), QPointF(0, 0));
    }

    printf("%s\n", failures == 0 ? "OK" : "FAIL");
    return failures == 0 ? 0 : 1;
}
