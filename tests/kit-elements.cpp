// The kit's elements, in the engine that draws them.
//
// These are the parts of the foundation that only exist in a browser: the
// property contract of a custom element, the stylesheet adopted into a shadow
// root, and what a slot does to a click. The library's own tests cannot see any
// of it, so it is checked here, against the same bundle a card ships.
//
// Verified by mutation: without the upgrade of a property set before the
// definition arrived, without the accessors that make `el.title = "x"` repaint,
// with the row's click handler back on the whole row, with the first-row border
// rule written as it was for the light DOM, with a swipe deleting rather than
// asking, or with a busy button still pressable, this turns red.

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

window.__removed = 0;
window.__opened = "";
window.__chose = "";
var swipe = document.createElement("hp-swipe-row");
swipe.id = "swipe";
swipe.setAttribute("title", "Home");
swipe.addEventListener("remove", function () { window.__removed++; });
swipe.addEventListener("open", function (e) { window.__opened = String(e.detail.open); });
probe.appendChild(swipe);

var instant = document.createElement("hp-swipe-row");
instant.id = "instant";
instant.setAttribute("title", "No confirmation");
instant.setAttribute("instant", "");
instant.addEventListener("remove", function () { window.__removed += 10; });
probe.appendChild(instant);

var choice = document.createElement("hp-choice");
choice.id = "choice";
choice.setAttribute("label", "When device sleeps");
choice.setAttribute("value", "off");
choice.choices = [{ value: "on", label: "Stay on" }, { value: "off", label: "Turn off" }];
choice.addEventListener("choose", function (e) { window.__chose = e.detail.value; });
probe.appendChild(choice);

var busy = document.createElement("hp-activity-button");
busy.id = "busy";
busy.setAttribute("label", "Join");
probe.appendChild(busy);

window.__changing = [];
window.__changed = [];
var slider = document.createElement("hp-slider");
slider.id = "slider";
slider.setAttribute("value", "50");
slider.addEventListener("changing", function (e) { window.__changing.push(e.detail.value); });
slider.addEventListener("change", function (e) { window.__changed.push(e.detail.value); });
probe.appendChild(slider);

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
    for (const QString& name : { QStringLiteral("main.js"), QStringLiteral("page.css"), QStringLiteral("kit.css"),
                                 QStringLiteral("theme-enyo.css") })
        QFile::copy(source + "/" + name, dir.filePath(name));
    QFile page(dir.filePath("index.html"));
    if (!page.open(QIODevice::WriteOnly))
        return 1;
    page.write("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
               "<link rel=\"stylesheet\" href=\"theme-enyo.css\">"
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
    check("and so does one that shows it is working",
          js("var r = document.createElement('hp-row'); var b = document.createElement('hp-activity-button');"
             "r.appendChild(b); document.getElementById('probe').appendChild(r);"
             "getComputedStyle(b).flexGrow"),
          QStringLiteral("1"));
    check("a card's own stylesheet cannot reach into a control",
          js("var s = document.createElement('style');"
             "s.textContent = '.hp-row-title { display: none }';"
             "document.head.appendChild(s);"
             "getComputedStyle(document.getElementById('second').shadowRoot.querySelector('.hp-row-title')).display"),
          QStringLiteral("block"));

    // Swipe to delete: HP's inline confirmation, and the switch that skips it.
    const auto swipe = [&](const char* id) {
        return QString("(function () { var el = document.getElementById('%1');"
                       "var r = el.shadowRoot.querySelector('.hp-swipe-row');"
                       "r.dispatchEvent(new PointerEvent('pointerdown', { clientX: 200 }));"
                       "r.dispatchEvent(new PointerEvent('pointerup', { clientX: 100 }));"
                       "return 1; })()").arg(id);
    };
    // A tap, or a finger that barely moved, is not a swipe: a list where
    // touching a row deletes it is a list nobody can use.
    js("(function () { var r = document.getElementById('swipe').shadowRoot.querySelector('.hp-swipe-row');"
       "r.dispatchEvent(new PointerEvent('pointerdown', { clientX: 200 }));"
       "r.dispatchEvent(new PointerEvent('pointerup', { clientX: 194 })); return 1; })()");
    check("a finger that barely moved is not a swipe",
          js("window.__opened + ':' + window.__removed"), QStringLiteral(":0"));

    js(qPrintable(swipe("swipe")));
    check("a swipe asks to open the confirmation, it does not delete",
          js("window.__opened + ':' + window.__removed"), QStringLiteral("true:0"));
    js("document.getElementById('swipe').open = true; 1");
    waitFor([&]() { return js("String(!!document.getElementById('swipe').shadowRoot.querySelector('.hp-swipe-confirm'))") == "true"; }, 2000);
    js("document.getElementById('swipe').shadowRoot.querySelector('.hp-button.negative').click(); 1");
    check("and the confirmation is what deletes", js("String(window.__removed)"), QStringLiteral("1"));
    js(qPrintable(swipe("instant")));
    check("a row that asks for no confirmation deletes on the swipe itself",
          js("String(window.__removed)"), QStringLiteral("11"));

    // One of a few, in the row itself.
    check("the chosen one is the one marked",
          js("document.getElementById('choice').shadowRoot.querySelector('.hp-choice-one.chosen').textContent"),
          QStringLiteral("Turn off"));
    js("document.getElementById('choice').shadowRoot.querySelectorAll('.hp-choice-one')[0].click(); 1");
    check("and choosing another says which", js("window.__chose"), QStringLiteral("on"));

    // A button that is working says so, and cannot be pressed twice.
    check("a button at rest has no spinner and can be pressed",
          js("var b = document.getElementById('busy').shadowRoot.querySelector('button');"
             "String(!b.disabled) + ':' + String(b.querySelectorAll('.hp-button-spinner').length)"),
          QStringLiteral("true:0"));
    js("document.getElementById('busy').busy = true; 1");
    waitFor([&]() { return js("String(document.getElementById('busy').shadowRoot.querySelectorAll('.hp-button-spinner').length)") == "1"; }, 2000);
    check("a busy one shows it and refuses another press",
          js("var b = document.getElementById('busy').shadowRoot.querySelector('button');"
             "String(b.disabled) + ':' + String(b.querySelectorAll('.hp-button-spinner').length)"),
          QStringLiteral("true:1"));

    // A slider: what it shows, and the two events a card writes to a service
    // with. The bar is 200 px wide in the probe, so a quarter along is 25.
    js("document.getElementById('slider').style.width = '200px'; 1");
    check("it fills to where its value is",
          js("document.getElementById('slider').shadowRoot.querySelector('.hp-slider-filled').style.width"),
          QStringLiteral("50%"));
    js("(function () { var s = document.getElementById('slider');"
       "var bar = s.shadowRoot.querySelector('.hp-slider-bar');"
       "var box = bar.getBoundingClientRect();"
       "var el = s.shadowRoot.querySelector('.hp-slider');"
       "el.dispatchEvent(new PointerEvent('pointerdown', { clientX: box.left + box.width * 0.25, bubbles: true }));"
       "el.dispatchEvent(new PointerEvent('pointermove', { clientX: box.left + box.width * 0.75, bubbles: true }));"
       "el.dispatchEvent(new PointerEvent('pointerup', { clientX: box.left + box.width * 0.75, bubbles: true }));"
       "return 1; })()");
    check("a finger dragging it says so as it goes", js("window.__changing.join(',')"), QStringLiteral("25,75"));
    check("and says what it settled on when it lifts", js("window.__changed.join(',')"), QStringLiteral("75"));
    js("(function () { var s = document.getElementById('slider');"
       "var el = s.shadowRoot.querySelector('.hp-slider');"
       "el.dispatchEvent(new PointerEvent('pointermove', { clientX: 0, bubbles: true }));"
       "return 1; })()");
    check("and a finger that is not down moves nothing", js("window.__changing.join(',')"),
          QStringLiteral("25,75"));

    // And the showcase itself is drawn by all of this.
    check("the showcase card drew its controls",
          js("String(document.querySelectorAll('#card hp-row, #card hp-button, #card hp-toggle').length > 10)"),
          QStringLiteral("true"));

    return failures == 0 ? 0 : 1;
}
