// The order application-wide event filters run in, which a hover depends on.
//
// MouseEventEater is installed on the QCoreApplication and swallows every mouse
// event aimed at the webOS surface -- that is how webOS gets its touches, since
// an ignored mouse event is what Qt's AA_SynthesizeTouchForUnhandledMouseEvents
// turns into a finger. A hover has to be read BEFORE that happens, and the only
// thing that makes it possible is Qt activating filters in reverse order of
// installation: the one installed last is offered the event first.
//
// HoverToMouseMove is therefore installed after the eater in Main.cpp. If Qt
// ever changed that rule, or someone reordered those two lines, hovers would
// stop reaching pages with nothing in the build to say so -- the shell would
// still start and everything else would still work. Hence this.
//
// It also checks the second half of the assumption: that a mouse move with no
// button held reaches an application filter at all.
//
// Runs headless:  ./filter-order -platform offscreen
#include <QApplication>
#include <QMouseEvent>
#include <QWidget>
#include <cstdio>
#include <vector>

static std::vector<int> g_order;

class Recorder : public QObject {
public:
    Recorder(int id, bool swallow) : m_id(id), m_swallow(swallow) {}
    int buttonsSeen = -1;
protected:
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::MouseMove) {
            g_order.push_back(m_id);
            buttonsSeen = (int) static_cast<QMouseEvent*>(e)->buttons();
            if (m_swallow) {
                // What MouseEventEater does: mark it unhandled and claim it.
                e->ignore();
                return true;
            }
        }
        return QObject::eventFilter(o, e);
    }
private:
    int m_id;
    bool m_swallow;
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QWidget surface;
    surface.resize(200, 200);

    // The order Main.cpp uses: the eater first, the hover reader after it.
    Recorder eater(1, true);
    Recorder hover(2, false);
    QCoreApplication::instance()->installEventFilter(&eater);
    QCoreApplication::instance()->installEventFilter(&hover);

    QMouseEvent move(QEvent::MouseMove, QPointF(10, 10), QPointF(10, 10),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&surface, &move);

    printf("filtros llamados en orden:");
    for (size_t i = 0; i < g_order.size(); ++i)
        printf(" %d", g_order[i]);
    printf("   (2 = instalado ultimo)\n");
    printf("botones vistos por el hover: %d  (0 = ninguno, que es lo que hace hover)\n",
           hover.buttonsSeen);

    const bool lastInstalledRunsFirst = !g_order.empty() && g_order[0] == 2;
    const bool hoverSawIt = hover.buttonsSeen == (int) Qt::NoButton;
    const bool eaterStillRuns = g_order.size() == 2 && g_order[1] == 1;

    const bool ok = lastInstalledRunsFirst && hoverSawIt && eaterStillRuns;
    printf("%s\n", ok ? "OK: el filtro instalado ultimo ve el hover antes de que lo traguen"
                      : "FAIL: el hover no llegaria al filtro, o el eater dejo de correr");
    return ok ? 0 : 1;
}
