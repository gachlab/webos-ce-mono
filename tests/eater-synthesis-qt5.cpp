// Pins down why WindowServer::deliverAsTouch has to exist.
//
// Main.cpp installs a global MouseEventEater that calls e->ignore() and returns
// true for EVERY mouse event. The intent is that, left unaccepted, Qt turns them
// into touches via AA_SynthesizeTouchForUnhandledMouseEvents, giving webOS the
// TouchBegin it needs to register a finger.
//
// It does not. Qt only synthesizes a touch from a mouse event that nobody
// accepted, and QGraphicsView's viewport accepts the press before the attribute
// ever gets a say -- so the eater swallows the mouse and nothing comes out the
// other side. That is the finding this test pins down, and the reason the shell
// builds its touch events by hand instead.
//
// So a PASS here means "no TouchBegin", which is the behaviour the workaround is
// built on. If this ever starts failing, Qt changed and deliverAsTouch may no
// longer be needed.
//
// Runs headless:  ./eater-synthesis-qt5 -platform offscreen
#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsObject>
#include <QTouchEvent>
#include <QMouseEvent>
#include <cstdio>

static int touches[3] = {0,0,0};   // begin, update, end
static int mice = 0;

class SpyItem : public QGraphicsObject {
public:
    SpyItem() { setAcceptTouchEvents(true); }
    QRectF boundingRect() const override { return QRectF(0,0,400,400); }
    void paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override {}
protected:
    bool sceneEvent(QEvent* e) override {
        switch (e->type()) {
        case QEvent::TouchBegin:  touches[0]++; e->accept(); return true;
        case QEvent::TouchUpdate: touches[1]++; e->accept(); return true;
        case QEvent::TouchEnd:    touches[2]++; e->accept(); return true;
        default: break;
        }
        return QGraphicsObject::sceneEvent(e);
    }
};

// Verbatim copy of Src/base/MouseEventEater.h
class MouseEventEater : public QObject {
protected:
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::MouseButtonRelease ||
            e->type() == QEvent::MouseButtonPress ||
            e->type() == QEvent::MouseButtonDblClick ||
            e->type() == QEvent::MouseMove) {
            mice++;
            e->ignore();
            return true;
        }
        return QObject::eventFilter(o, e);
    }
};

int main(int argc, char** argv)
{
    QCoreApplication::setAttribute(Qt::AA_SynthesizeTouchForUnhandledMouseEvents, true);
    QApplication app(argc, argv);

    QGraphicsScene scene(0,0,400,400);
    scene.addItem(new SpyItem);
    QGraphicsView view(&scene);
    view.viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    view.resize(400,400);
    view.show();
    app.processEvents();

    MouseEventEater* eater = new MouseEventEater;
    QCoreApplication::instance()->installEventFilter(eater);

    // A full click, as a user would make it.
    const QPoint p(100,100);
    QMouseEvent press(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent rel(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &press);
    QApplication::sendEvent(view.viewport(), &rel);
    app.processEvents();

    printf("mouse events eaten: %d\n", mice);
    printf("TouchBegin:%d  TouchUpdate:%d  TouchEnd:%d\n", touches[0], touches[1], touches[2]);
    const bool asExpected = (mice > 0 && touches[0] == 0);
    printf("%s\n", asExpected
        ? "OK: the eater swallows the mouse and no TouchBegin comes out,\n"
          "    which is why WindowServer::deliverAsTouch builds them by hand"
        : "FAIL: Qt now synthesizes a TouchBegin here -- recheck deliverAsTouch");
    return asExpected ? 0 : 1;
}
