// Whether the shell's window can be resized at all, without bringing webOS up.
//
// Three separate things held it at one size, and they are not equals:
// HostQtDesktop::show called m_widget->setFixedSize, WindowServer called
// setFixedSize on the view, and the window's own layout carried
// QLayout::SetFixedSize. The last one outranks the other two -- it makes the
// layout's sizeHint both the minimum and the maximum of the window -- so while
// it stands, removing both setFixedSize calls changes nothing at all. That is
// what makes this worth a test rather than a comment: the fix looks done, the
// window still will not move, and nothing points at the reason.
//
// This reproduces HostQtDesktop's structure -- a window whose QVBoxLayout holds
// the view with the gesture strip below it -- and checks both halves: that the
// constraint really does pin the window, and that without it a resize both
// takes effect and arrives as a QEvent::Resize carrying the numbers the display
// arithmetic needs (the window minus the strip, as init() computes it).
//
// Runs headless:  ./window-resize -platform offscreen
#include <QApplication>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QVBoxLayout>
#include <QWidget>
#include <QEvent>
#include <cstdio>

static const int kStripHeight = 40;

static const int kStartWidth  = 800;
static const int kStartHeight = 600;
static const int kEndWidth    = 1024;
static const int kEndHeight   = 768;

// The same shape as HostQtDesktop::eventFilter: it watches the window and works
// the display size out of it.
class ResizeSpy : public QObject
{
public:
    int resizes = 0;
    int displayWidth = 0;
    int displayHeight = 0;

protected:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (event->type() == QEvent::Resize) {
            QWidget* window = static_cast<QWidget*>(object);
            resizes++;
            displayWidth = window->width();
            displayHeight = window->height() - kStripHeight;
        }
        return QObject::eventFilter(object, event);
    }
};

static QWidget* buildWindow(QGraphicsScene* scene, bool pinned, ResizeSpy* spy)
{
    QWidget* window = new QWidget;
    QGraphicsView* view = new QGraphicsView(scene);
    QWidget* strip = new QWidget;
    strip->setFixedHeight(kStripHeight);

    QVBoxLayout* layout = new QVBoxLayout(window);
    if (pinned) {
        view->setFixedSize(kStartWidth, kStartHeight);
        layout->setSizeConstraint(QLayout::SetFixedSize);
    }
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(view);
    layout->addWidget(strip);

    window->resize(kStartWidth, kStartHeight + kStripHeight);
    if (spy)
        window->installEventFilter(spy);
    window->show();
    return window;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QGraphicsScene scene(0, 0, kStartWidth, kStartHeight);

    // 1. As it was: the layout constraint holds the window down, and asking for
    //    a new size is simply ignored.
    QWidget* pinned = buildWindow(&scene, true, 0);
    app.processEvents();
    pinned->resize(kEndWidth, kEndHeight + kStripHeight);
    app.processEvents();
    const bool stuck = (pinned->width() == kStartWidth);
    printf("pinned:  asked for %d wide, got %d  %s\n",
           kEndWidth, pinned->width(),
           stuck ? "<- held, as QLayout::SetFixedSize does" : "<- moved: the pin is gone");

    // 2. As it is: the resize takes effect and is reported with the size the
    //    display is computed from.
    ResizeSpy spy;
    QWidget* free = buildWindow(&scene, false, &spy);
    app.processEvents();
    free->resize(kEndWidth, kEndHeight + kStripHeight);
    app.processEvents();

    const bool resized = (free->width() == kEndWidth &&
                          free->height() == kEndHeight + kStripHeight);
    const bool reported = (spy.resizes > 0 &&
                           spy.displayWidth == kEndWidth &&
                           spy.displayHeight == kEndHeight);

    printf("free:    window %dx%d, %d resize event(s), display %dx%d\n",
           free->width(), free->height(), spy.resizes,
           spy.displayWidth, spy.displayHeight);

    const bool ok = stuck && resized && reported;
    printf("%s\n", ok
        ? "OK: the constraint pins the window, and without it the resize arrives with the display size"
        : "FAILED: see which of the three lines above disagrees");
    return ok ? 0 : 1;
}
