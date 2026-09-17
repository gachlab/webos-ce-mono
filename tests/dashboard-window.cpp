// What a dashboard window needs from the compat layer, all three found live
// with notifications on the running shell.
//
//   1. window.open()'s "attributes=" reaches the page it creates. enyo and Mojo
//      open every dashboard with attributes={"window":"dashboard",...};
//      QtWebEngine never hands the third argument to createWindow(), so
//      WebAppMgr saw no type and every dashboard became a full-screen card.
//   2. A transparent page renders without a background. The dashboard's page is
//      transparent so the shell's dark menu shows through; QWidget::grab()
//      filled the palette's light grey under it first.
//   3. The flex-width repair leaves a zero width alone unless FlexLayout wrote
//      it. The dashboard hides the notifications under the top one in boxes
//      0px wide with overflow hidden; clearing those drew all three on top of
//      each other.
//
// Verified by mutation: dropping the window.open script, keeping it out of
// frames, handing over the whole features string, grabbing with the
// background, or clearing every zero width each turn this red.

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QPainter>
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

static int failures = 0;

static void check(const char* what, const QString& got, const QString& expected)
{
    const bool ok = got == expected;
    if (!ok)
        ++failures;
    std::printf("%-56s %-24s %s\n", what, qPrintable(got),
                ok ? "ok" : qPrintable("FAILED, wanted " + expected));
}

static bool writeFile(const QString& path, const QString& text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream(&file) << text;
    return true;
}

// Shaped like SysMgrWebPage::createWindow: a new page for every window.
class Opener : public QWebPage
{
public:
    QList<QWebPage*> children;

    QWebPage* createWindow(WebWindowType) override
    {
        children << new QWebPage(this);
        return children.last();
    }
};

static bool loaded(QWebPage& page, const QString& path)
{
    bool done = false;
    QObject::connect(&page, &QWebPage::loadFinished, &page, [&done]() { done = true; });
    page.mainFrame()->setUrl(QUrl::fromLocalFile(path));
    return waitFor([&]() { return done; }, 15000);
}

static void windowAttributes(const QTemporaryDir& dir)
{
    writeFile(dir.filePath("child.html"), QStringLiteral("<html><body></body></html>\n"));
    writeFile(dir.filePath("opener.html"), QStringLiteral(
        "<html><body><iframe id='frame' src='child.html'></iframe></body></html>\n"));

    Opener opener;
    if (!loaded(opener, dir.filePath("opener.html"))) {
        check("the opener loads", "no", "yes");
        return;
    }
    const auto open = [&](const QString& call) {
        const qsizetype before = opener.children.size();
        opener.mainFrame()->evaluateJavaScript(call);
        waitFor([&]() { return opener.children.size() > before; }, 15000);
        return opener.children.size() > before ? opener.children.last()->attributes()
                                               : QStringLiteral("<no window>");
    };

    // What enyo.windows.agent.open() passes, commas inside the JSON included.
    check("a dashboard's attributes reach its page",
          open(R"(window.open("child.html", "a", 'height=52, attributes={"window":"dashboard","icon":"i.png"}'); 1)"),
          R"({"window":"dashboard","icon":"i.png"})");
    check("a window opened without them has none",
          open(R"(window.open("child.html", "b"); 1)"), "");
    check("nor one whose features carry no attributes",
          open(R"(window.open("child.html", "c", "height=52"); 1)"), "");
    check("and a frame's window.open counts too",
          open(R"(document.getElementById("frame").contentWindow
                  .open("child.html", "d", 'attributes={"window":"popupalert"}'); 1)"),
          R"({"window":"popupalert"})");
}

static QImage rendered(QWebPage& page)
{
    QImage image(page.viewportSize(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    page.mainFrame()->render(&painter, QWebFrame::AllLayers, QRegion());
    return image;
}

static void transparentRender(const QTemporaryDir& dir)
{
    writeFile(dir.filePath("dash.html"), QStringLiteral(
        "<html><body style='margin:0;background:transparent'>"
        "<div style='width:20px;height:20px;background:#f00'></div></body></html>\n"));

    QWebPage page;
    QPalette palette = page.palette();
    palette.setBrush(QPalette::Base, Qt::transparent);   // WindowedWebApp::attach
    page.setPalette(palette);
    page.setViewportSize(QSize(100, 100));
    if (!loaded(page, dir.filePath("dash.html"))) {
        check("the transparent page loads", "no", "yes");
        return;
    }

    // The first frames can still be blank; wait for the page's own pixels.
    QImage image;
    waitFor([&]() { image = rendered(page); return QColor(image.pixel(5, 5)) == QColor(Qt::red); }, 15000);
    check("a transparent page paints its content",
          QColor::fromRgba(image.pixel(5, 5)).name(QColor::HexArgb), "#ffff0000");
    check("and nothing where it has none",
          QString::number(qAlpha(image.pixel(60, 60))), "0");

    // An ordinary page is untouched: opaque, as before.
    QWebPage opaque;
    opaque.setViewportSize(QSize(100, 100));
    writeFile(dir.filePath("card.html"), QStringLiteral("<html><body></body></html>\n"));
    if (loaded(opaque, dir.filePath("card.html"))) {
        QImage card;
        waitFor([&]() { card = rendered(opaque); return qAlpha(card.pixel(60, 60)) == 255; }, 15000);
        check("an opaque page still paints its background",
              QString::number(qAlpha(card.pixel(60, 60))), "255");
    }
}

static void zeroWidths(const QTemporaryDir& dir)
{
    // One box as FlexLayout writes it -- a flex and a zero width -- in a box
    // sized by its content, where Chromium gives the flex nothing and the label
    // disappears (Contacts' type pickers). And one clipped on purpose, as the
    // dashboard does.
    writeFile(dir.filePath("widths.html"), QStringLiteral(
        "<html><body style='margin:0'><div style='display:-webkit-inline-box'>"
        "<div id='flexed' style='-webkit-box-flex:1;width:0px'>"
        "<span style='white-space:nowrap'>MOBILE</span></div>"
        "<div id='clipped' style='width:0px;overflow:hidden'>"
        "<div style='width:270px'>Luis P&eacute;rez</div></div>"
        "</div></body></html>\n"));

    QWebPage page;
    if (!loaded(page, dir.filePath("widths.html"))) {
        check("the page with zero widths loads", "no", "yes");
        return;
    }
    const auto widthOf = [&](const char* id) {
        return page.mainFrame()->evaluateJavaScript(
            QString("document.getElementById('%1').style.width").arg(id)).toString();
    };
    waitFor([&]() { return widthOf("flexed").isEmpty(); }, 5000);
    check("a width FlexLayout starved is cleared", widthOf("flexed"), "");
    check("a zero width without a flex is left alone", widthOf("clipped"), "0px");
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("no temporary directory\n");
        return 1;
    }

    windowAttributes(dir);
    transparentRender(dir);
    zeroWidths(dir);

    return failures == 0 ? 0 : 1;
}
