// The kit's elements, in the engine that draws them.
//
// These are the parts of the foundation that only exist in a browser: the
// property contract of a custom element, the stylesheet adopted into a shadow
// root, and what a slot does to a click. The library's own tests cannot see any
// of it, so it is checked here, against the same bundle a card ships.
//
// Verified by mutation: without the upgrade of a property set before the
// definition arrived, without the accessors that make `el.title = "x"` repaint,
// with the row's click handler back on the whole row, or with the first-row
// border rule written as it was for the light DOM, this turns red.

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>

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
    std::printf("%-60s %-18s %s\n", what, qPrintable(got.left(18)),
                ok ? "ok" : qPrintable("FAILED, wanted " + expected));
}

// Built before the bundle runs, which is the case a custom element has to
// survive: the markup is parsed, properties are set, and the definition only
// arrives afterwards.
static const char kBeforeUpgrade[] = R"JS(
window.__selected = 0;
var probe = document.createElement("div");
probe.id = "probe";
document.body.appendChild(probe);

var early = document.createElement("hp-selector");
early.id = "early";
early.setAttribute("label", "When to connect");
early.setAttribute("value", "ask");
early.choices = [
    { value: "off", label: "Never" },
    { value: "ask", label: "Always ask" },
    { value: "auto", label: "Automatically" },
];
probe.appendChild(early);

var list = document.createElement("div");
list.className = "hp-list";
list.innerHTML = "<hp-row id='first' title='One'><hp-toggle id='inside'></hp-toggle></hp-row>" +
                 "<hp-row id='second' title='Two'></hp-row>" +
                 "<hp-row id='third' title='Three'><hp-button id='wide' label='Wide'></hp-button></hp-row>";
probe.appendChild(list);
document.addEventListener("select", function () { window.__selected++; });
)JS";

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const QString built = QString::fromLocal8Bit(qgetenv("WEBOS_CARDS_BUILD"));
    if (built.isEmpty() || !QFile::exists(built + "/com.palm.app.kit/main.js")) {
        std::printf("SKIP: the cards are not built (tools/build-cards.sh)\n");
        return 77;
    }

    QTemporaryDir dir;
    const QString source = built + "/com.palm.app.kit";
    for (const QString& name : { QStringLiteral("main.js"), QStringLiteral("page.css"), QStringLiteral("kit.css") })
        QFile::copy(source + "/" + name, dir.filePath(name));
    QFile page(dir.filePath("index.html"));
    if (!page.open(QIODevice::WriteOnly))
        return 1;
    page.write("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
               "<link rel=\"stylesheet\" href=\"page.css\"></head><body><div id=\"card\"></div>"
               "<script>");
    page.write(kBeforeUpgrade);
    page.write("</script><script src=\"main.js\"></script></body></html>");
    page.close();

    QWebPage card;
    bool loaded = false;
    QObject::connect(&card, &QWebPage::loadFinished, &card, [&loaded]() { loaded = true; });
    card.mainFrame()->load(QUrl::fromLocalFile(dir.filePath("index.html")));
    if (!waitFor([&]() { return loaded; }, 20000)) {
        std::printf("the page never loaded\n");
        return 1;
    }
    const auto js = [&](const char* code) { return card.mainFrame()->evaluateJavaScript(code).toString(); };
    waitFor([&]() { return js("String(!!document.getElementById('early').shadowRoot)") == "true"; }, 5000);

    // A property set before the definition arrived is not lost.
    check("a property set before the element was defined is taken",
          js("document.getElementById('early').shadowRoot.querySelector('.hp-selector-value').textContent"),
          QStringLiteral("Always ask"));
    check("and it reads back as a property", js("String(document.getElementById('early').choices.length)"),
          QStringLiteral("3"));

    // Setting a property repaints, which is what every binding relies on.
    js("document.getElementById('second').title = 'Renamed'; 1");
    waitFor([&]() {
        return js("document.getElementById('second').shadowRoot.querySelector('.hp-row-title').textContent")
               == "Renamed";
    }, 2000);
    check("setting a property repaints the element",
          js("document.getElementById('second').shadowRoot.querySelector('.hp-row-title').textContent"),
          QStringLiteral("Renamed"));
    check("and an attribute still does too",
          js("document.getElementById('second').setAttribute('detail', 'From an attribute');"
             "document.getElementById('second').shadowRoot.querySelector('.hp-row-detail') ? 'later' : 'none'"),
          QStringLiteral("later"));

    // A control the card put in a row is not the row.
    js("document.getElementById('inside').shadowRoot.querySelector('button').click(); 1");
    check("tapping a control inside a row does not select the row", js("String(window.__selected)"),
          QStringLiteral("0"));
    js("document.getElementById('first').shadowRoot.querySelector('.hp-row-text').click(); 1");
    check("tapping the row itself does", js("String(window.__selected)"), QStringLiteral("1"));

    // The stylesheet means the same thing inside a shadow root as it did
    // outside one.
    check("the first row in a list has no line above it",
          js("getComputedStyle(document.getElementById('first').shadowRoot.querySelector('.hp-row')).borderTopWidth"),
          QStringLiteral("0px"));
    check("and the ones after it do",
          js("getComputedStyle(document.getElementById('second').shadowRoot.querySelector('.hp-row')).borderTopWidth"),
          QStringLiteral("1px"));
    check("a button put into a row takes the room the row has left",
          js("getComputedStyle(document.getElementById('wide')).flexGrow"), QStringLiteral("1"));
    check("a card's own stylesheet cannot reach into a control",
          js("var s = document.createElement('style');"
             "s.textContent = '.hp-row-title { display: none }';"
             "document.head.appendChild(s);"
             "getComputedStyle(document.getElementById('second').shadowRoot.querySelector('.hp-row-title')).display"),
          QStringLiteral("block"));

    // And the showcase itself is drawn by all of this.
    check("the showcase card drew its controls",
          js("String(document.querySelectorAll('#card hp-row, #card hp-button, #card hp-toggle').length > 10)"),
          QStringLiteral("true"));

    return failures == 0 ? 0 : 1;
}
