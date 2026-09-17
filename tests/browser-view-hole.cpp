// The browser's content area stops being painted when its app hides it, and
// comes back when the app shows it again.
//
// The compat layer paints the embedded page over a box in the browser app's
// document. enyo's Pane shows another view -- Preferences -- by hiding the
// browser's with display:none, and nothing tells the box. Found live: the
// Preferences opened over a loaded page, and the page went on being painted
// over them.
//
// A page here plays the browser app: an enyo.BasicWebView of its own, and a
// BrowserViewFactory that records every rect the box sends.
//
// Verified by mutation: without the ResizeObserver this turns red.

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QRect>
#include <QTemporaryDir>
#include <QTextStream>

#include <QWebFrame>
#include <QWebPage>

#include <cstdio>
#include <functional>

static bool waitFor(const std::function<bool()>& done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

// What BrowserViewAdapter offers the script, recording the geometry.
class View : public QObject
{
    Q_OBJECT
public:
    QList<QRect> rects;
    Q_INVOKABLE void setGeometry(int x, int y, int width, int height) { rects.append(QRect(x, y, width, height)); }
    Q_INVOKABLE QString url() const { return QString(); }
    Q_INVOKABLE bool canGoBack() const { return false; }
    Q_INVOKABLE bool canGoForward() const { return false; }
Q_SIGNALS:
    void loadStarted();
    void loadProgress(int progress);
    void loadFinished(bool ok);
    void titleChanged(const QString& title);
};

class Factory : public QObject
{
    Q_OBJECT
public:
    View* view = nullptr;
    Q_INVOKABLE QObject* create()
    {
        view = new View;
        view->setParent(this);
        return view;
    }
};

static int failures = 0;

static void check(const char* what, bool ok, const QString& detail = QString())
{
    if (!ok)
        ++failures;
    std::printf("%-56s %s %s\n", what, ok ? "ok" : "FAILED", qPrintable(detail));
}

static QString last(const View* view)
{
    if (!view || view->rects.isEmpty())
        return "none";
    const QRect r = view->rects.last();
    return QString("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QTemporaryDir dir;
    if (!dir.isValid())
        return 1;
    {
        QFile file(dir.filePath("browser.html"));
        file.open(QIODevice::WriteOnly | QIODevice::Text);
        QTextStream(&file) << R"HTML(<html><body style="margin:0">
<div id="pane"><div id="hole" style="position:relative;left:10px;top:20px;width:300px;height:200px"></div></div>
<script>
  // As enyo does: the kind is complete before it is assigned.
  var BasicWebView = function () {};
  BasicWebView.prototype.rendered = function () {};
  window.enyo = {};
  enyo.BasicWebView = BasicWebView;
  var control = new enyo.BasicWebView();
  control.hasNode = function () { return document.getElementById("hole"); };
  control.rendered();
  window.hide = function () { document.getElementById("pane").style.display = "none"; };
  window.show = function () { document.getElementById("pane").style.display = ""; };
</script></body></html>
)HTML";
    }

    QWebPage page;
    page.setViewportSize(QSize(800, 600));
    Factory factory;
    QObject::connect(page.mainFrame(), &QWebFrame::javaScriptWindowObjectCleared, page.mainFrame(), [&]() {
        page.mainFrame()->addToJavaScriptWindowObject("BrowserViewFactory", &factory);
    });
    bool loaded = false;
    QObject::connect(&page, &QWebPage::loadFinished, &page, [&loaded]() { loaded = true; });
    page.mainFrame()->setUrl(QUrl::fromLocalFile(dir.filePath("browser.html")));
    if (!waitFor([&]() { return loaded; }, 15000)) {
        std::printf("the page did not load\n");
        return 1;
    }

    waitFor([&]() { return factory.view && last(factory.view) == "10,20 300x200"; }, 5000);
    check("the box's rect is sent", last(factory.view) == "10,20 300x200", last(factory.view));

    page.mainFrame()->evaluateJavaScript("hide(); 1");
    waitFor([&]() { return last(factory.view) == "0,0 0x0"; }, 5000);
    check("hiding the app's view stops the painting", last(factory.view) == "0,0 0x0", last(factory.view));

    page.mainFrame()->evaluateJavaScript("show(); 1");
    waitFor([&]() { return last(factory.view) == "10,20 300x200"; }, 5000);
    check("showing it again puts the page back", last(factory.view) == "10,20 300x200", last(factory.view));

    return failures == 0 ? 0 : 1;
}

#include "browser-view-hole.moc"
