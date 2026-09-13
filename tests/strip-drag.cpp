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

    printf("lo que el filtro DEBE reclamar (el hueco que HP dejo sin atender)\n");
    check("arrastre largo hacia arriba", QPoint(100, 30), QPoint(100, 0),
          StripDragToCoreNavi::Up);
    check("arrastre largo hacia abajo", QPoint(100, 0), QPoint(100, 30),
          StripDragToCoreNavi::Down);
    check("vertical con algo de deriva lateral", QPoint(100, 40), QPoint(108, 0),
          StripDragToCoreNavi::Up);

    printf("\nlo que DEBE dejarle a HP (su rama horizontal ya funciona)\n");
    check("arrastre a la izquierda", QPoint(100, 10), QPoint(40, 10),
          StripDragToCoreNavi::None);
    check("arrastre a la derecha", QPoint(40, 10), QPoint(100, 10),
          StripDragToCoreNavi::None);
    check("diagonal con dominancia horizontal", QPoint(100, 10), QPoint(40, 30),
          StripDragToCoreNavi::None);
    // The tie goes to HP: decide() uses >= so a perfect diagonal is his.
    check("diagonal exacta, empate", QPoint(0, 0), QPoint(30, 30),
          StripDragToCoreNavi::None);

    printf("\nlo que NADIE debe reclamar (bajo el umbral de HP: distancia^2 > 100)\n");
    check("toque sin mover", QPoint(50, 20), QPoint(50, 20),
          StripDragToCoreNavi::None);
    check("temblor de 3px", QPoint(50, 20), QPoint(50, 17),
          StripDragToCoreNavi::None);
    check("justo en el umbral (10px, 100 no supera 100)", QPoint(50, 20), QPoint(50, 10),
          StripDragToCoreNavi::None);
    check("un pixel por encima del umbral", QPoint(50, 21), QPoint(50, 10),
          StripDragToCoreNavi::Up);

    printf("\n%s\n", g_failures == 0
           ? "OK: reclama el hueco vertical y respeta el horizontal de HP"
           : "FAIL: el filtro pisaria gestos que no le tocan");
    return g_failures == 0 ? 0 : 1;
}
