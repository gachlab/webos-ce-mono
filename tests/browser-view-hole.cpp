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
// Verified by mutation: without the ResizeObserver, without the cutouts,
// without measuring a covered box again, or cutting out a menu's shadow, this
// turns red.

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QRect>
#include <QStringList>
#include <QVariantList>
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
    QStringList cutouts;
    Q_INVOKABLE void setGeometry(int x, int y, int width, int height) { rects.append(QRect(x, y, width, height)); }
    Q_INVOKABLE void setCutouts(const QVariantList& list)
    {
        QStringList parts;
        for (const QVariant& r : list) {
            QStringList n;
            for (const QVariant& v : r.toList())
                n << QString::number(v.toInt());
            parts << n.join(",");
        }
        cutouts << parts.join(" ");
    }
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
        // A menu's border image: 4 px of see-through shadow around an opaque
        // panel, 10 px slices.
        QImage frame(30, 30, QImage::Format_ARGB32);
        frame.fill(QColor(0, 0, 0, 20));
        for (int y = 4; y < 26; ++y)
            for (int x = 4; x < 26; ++x)
                frame.setPixelColor(x, y, QColor(200, 200, 200, 255));
        frame.save(dir.filePath("menu.png"));
    }
    {
        QFile file(dir.filePath("browser.html"));
        file.open(QIODevice::WriteOnly | QIODevice::Text);
        QTextStream(&file) << R"HTML(<html><body style="margin:0">
<div id="pane"><div id="hole" style="position:relative;left:10px;top:20px;width:300px;height:200px"></div></div>
<div id="prefs" style="position:absolute;left:0;top:0;width:200px;height:100px;display:none"></div>
<div id="menu" style="position:absolute;left:100px;top:100px;width:80px;height:60px;display:none;
     border:20px solid transparent;border-image:url(menu.png) 10 fill;box-sizing:border-box"></div>
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
  // A menu is an enyo.Popup, which tells the view when it shows; here the
  // view's resize stands for that.
  window.openMenu = function () { document.getElementById("menu").style.display = "block"; control.resize(); };
  window.closeMenu = function () { document.getElementById("menu").style.display = "none"; control.resize(); };
  // enyo's Pane going back: the view shows while the other still covers it.
  window.showUnderPrefs = function () {
    document.getElementById("prefs").style.display = "block";
    show();
    setTimeout(function () { document.getElementById("prefs").style.display = "none"; }, 300);
  };
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

    // Something of the app's over part of the box: that part is cut out, and
    // the rest of the page is still painted.
    page.mainFrame()->evaluateJavaScript("hide(); 1");
    waitFor([&]() { return last(factory.view) == "0,0 0x0"; }, 5000);
    factory.view->cutouts.clear();
    page.mainFrame()->evaluateJavaScript("showUnderPrefs(); 1");
    waitFor([&]() { return factory.view->cutouts.contains("10,20,190,80") && last(factory.view) == "10,20 300x200"; }, 5000);
    check("what covers the box is cut out of it", factory.view->cutouts.contains("10,20,190,80"),
          factory.view->cutouts.join(" | "));
    check("while the page stays painted", last(factory.view) == "10,20 300x200", last(factory.view));
    waitFor([&]() { return !factory.view->cutouts.isEmpty() && factory.view->cutouts.last().isEmpty(); }, 5000);
    check("and once it goes away, nothing is cut out",
          !factory.view->cutouts.isEmpty() && factory.view->cutouts.last().isEmpty(),
          factory.view->cutouts.join(" | "));

    // A menu with a shadow: only its opaque part is cut out. 4 of the image's
    // 10 px are shadow, at 20 px borders that is 8 px on each side.
    factory.view->cutouts.clear();
    page.mainFrame()->evaluateJavaScript("openMenu(); 1");
    waitFor([&]() { return factory.view->cutouts.contains("108,108,64,44"); }, 5000);
    check("a menu's shadow is not cut out, its panel is", factory.view->cutouts.contains("108,108,64,44"),
          factory.view->cutouts.join(" | "));
    page.mainFrame()->evaluateJavaScript("closeMenu(); 1");

    return failures == 0 ? 0 : 1;
}

#include "browser-view-hole.moc"
