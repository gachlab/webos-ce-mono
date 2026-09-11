// Test of the home button under Qt5, without bringing webOS up.
//
// The gesture strip (GestureStrip, in HostQtDesktop.cpp) holds a QPushButton
// that on click calls postGesture(Key_CoreNavi_Home), which posts a QKeyEvent.
// This reproduces that structure -- a Qt::NoFocus button inside a Qt::NoFocus
// widget, plus a separate view -- and checks who actually receives the key.
//
// Runs headless:  ./home-gesture-qt5 -platform offscreen
#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QPushButton>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <cstdio>

static int keysInView = 0;

class SpyView : public QGraphicsView {
public:
    SpyView(QGraphicsScene* e) : QGraphicsView(e) {}
protected:
    void keyPressEvent(QKeyEvent* e) override { keysInView++; QGraphicsView::keyPressEvent(e); }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QGraphicsScene scene(0, 0, 400, 400);
    SpyView view(&scene);
    view.resize(400, 400);
    view.show();

    // Same as GestureStrip: container and button, both without focus.
    QWidget strip;
    strip.setFocusPolicy(Qt::NoFocus);
    QPushButton* home = new QPushButton(&strip);
    home->setFocusPolicy(Qt::NoFocus);
    QVBoxLayout* l = new QVBoxLayout(&strip);
    l->addWidget(home);
    strip.show();

    app.processEvents();

    // 1. What the original code did.
    QWidget* focused = QApplication::focusWidget();
    printf("focusWidget() = %p  %s\n", (void*)focused,
           focused ? "" : "<- null: the key is lost in the if (window)");

    // 2. What it does now: post to the view.
    QApplication::postEvent(&view, new QKeyEvent(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier));
    QApplication::postEvent(&view, new QKeyEvent(QEvent::KeyRelease, Qt::Key_Home, Qt::NoModifier));
    app.processEvents();
    printf("posting to the view -> keyPressEvent received: %d\n", keysInView);

    bool ok = (focused == 0) && (keysInView == 1);
    printf("%s\n", ok
        ? "OK: focusWidget() is null (that was the bug) and the view DOES get the key"
        : "check: unexpected result");
    return ok ? 0 : 1;
}
