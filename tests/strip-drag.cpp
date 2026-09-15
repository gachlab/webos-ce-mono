// What a drag on the gesture strip means, and what it must refuse to mean.
//
// The strip below the screen is HP's stand-in for the gesture area a Pre had
// under its display, and half of it never fired on this port: dragging sideways
// posts Back, Menu, Previous or Next, while dragging up or down does nothing,
// because the vertical case was delegated to a flick gesture whose recogniser
// reads touch events that a handled mouse drag never produces. Measured in a
// running session: Back posted 7 times, Launcher 0.
//
// StripDragToCoreNavi restores the vertical half from an application filter.
// The risk in doing that is not the plumbing, it is claiming drags that belong
// to HP: if this decided that a sideways drag was vertical, it would fire
// Launcher on top of HP's Back and the strip would do two things at once.
//
// So what is pinned here is the boundary. The thresholds are HP's own, copied
// from the horizontal branch that works.
//
// Runs headless:  ./strip-drag -platform offscreen
#include <QCoreApplication>
#include <QPoint>
#include <cstdio>

#include "StripDragToCoreNavi.h"

static int g_failures = 0;

static const char* name(StripDragToCoreNavi::Direction d)
{
    switch (d) {
    case StripDragToCoreNavi::Up:   return "Up";
    case StripDragToCoreNavi::Down: return "Down";
    default:                        return "None";
    }
}

static void check(const char* what, QPoint down, QPoint up,
                  StripDragToCoreNavi::Direction expected)
{
    const StripDragToCoreNavi::Direction got =
        StripDragToCoreNavi::decide(down, up);
    const bool ok = got == expected;
    printf("  %-42s (%d,%d)->(%d,%d)  %-4s %s\n",
           what, down.x(), down.y(), up.x(), up.y(), name(got),
           ok ? "OK" : "<-- FAIL");
    if (!ok)
        g_failures++;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    printf("what the filter MUST claim (the gap HP left unhandled)\n");
    check("long drag up", QPoint(100, 30), QPoint(100, 0),
          StripDragToCoreNavi::Up);
    check("long drag down", QPoint(100, 0), QPoint(100, 30),
          StripDragToCoreNavi::Down);
    check("vertical with some sideways drift", QPoint(100, 40), QPoint(108, 0),
          StripDragToCoreNavi::Up);

    printf("\nwhat it MUST leave to HP (its horizontal branch already works)\n");
    check("drag left", QPoint(100, 10), QPoint(40, 10),
          StripDragToCoreNavi::None);
    check("drag right", QPoint(40, 10), QPoint(100, 10),
          StripDragToCoreNavi::None);
    check("diagonal, mostly horizontal", QPoint(100, 10), QPoint(40, 30),
          StripDragToCoreNavi::None);
    // The tie goes to HP: decide() uses >= so a perfect diagonal is HP's.
    check("exact diagonal, a tie", QPoint(0, 0), QPoint(30, 30),
          StripDragToCoreNavi::None);

    printf("\nwhat NOBODY may claim (under HP's threshold: distance^2 > 100)\n");
    check("tap without moving", QPoint(50, 20), QPoint(50, 20),
          StripDragToCoreNavi::None);
    check("3px tremor", QPoint(50, 20), QPoint(50, 17),
          StripDragToCoreNavi::None);
    check("exactly at the threshold (10px, 100 is not > 100)", QPoint(50, 20), QPoint(50, 10),
          StripDragToCoreNavi::None);
    check("one pixel over the threshold", QPoint(50, 21), QPoint(50, 10),
          StripDragToCoreNavi::Up);

    printf("\n%s\n", g_failures == 0
           ? "OK: claims the vertical gap and leaves HP's horizontal alone"
           : "FAIL: the filter would take gestures that are not its own");
    return g_failures == 0 ? 0 : 1;
}
