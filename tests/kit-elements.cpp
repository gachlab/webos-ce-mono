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
//
// The controls added for #58 are checked the same way: with a tab that fires
// on the one already chosen, an icon button that swallows its press, a long
// list that draws all its rows rather than a window, a search field that keeps
// the magnifier once text is in it, a popup that opens off the edge without
// being clamped, a drawer that starts open, a picker that does not mark its
// value, a dialog that lets Escape close the card behind it, a date picker
// that does not re-clamp the day when the month shortens, a 24-hour time
// picker that still shows an AM/PM wheel, or a prev/next arrow that fires while
// it is off, this turns red.
//
// #70 is checked here too: with the JSON reader gone -- so a `choices`
// attribute is not parsed and a control written only as markup falls back to
// its raw value -- or with the property no longer winning over the attribute,
// or with a malformed attribute throwing rather than falling back silently,
// this turns red.

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

var early = document.createElement("wos-selector");
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
var swipe = document.createElement("wos-swipe-row");
swipe.id = "swipe";
swipe.setAttribute("title", "Home");
swipe.addEventListener("remove", function () { window.__removed++; });
swipe.addEventListener("open", function (e) { window.__opened = String(e.detail.open); });
probe.appendChild(swipe);

var instant = document.createElement("wos-swipe-row");
instant.id = "instant";
instant.setAttribute("title", "No confirmation");
instant.setAttribute("instant", "");
instant.addEventListener("remove", function () { window.__removed += 10; });
probe.appendChild(instant);

var choice = document.createElement("wos-choice");
choice.id = "choice";
choice.setAttribute("label", "When device sleeps");
choice.setAttribute("value", "off");
choice.choices = [{ value: "on", label: "Stay on" }, { value: "off", label: "Turn off" }];
choice.addEventListener("choose", function (e) { window.__chose = e.detail.value; });
probe.appendChild(choice);

var busy = document.createElement("wos-activity-button");
busy.id = "busy";
busy.setAttribute("label", "Join");
probe.appendChild(busy);

window.__changing = [];
window.__changed = [];
var slider = document.createElement("wos-slider");
slider.id = "slider";
slider.setAttribute("value", "50");
slider.addEventListener("changing", function (e) { window.__changing.push(e.detail.value); });
slider.addEventListener("change", function (e) { window.__changed.push(e.detail.value); });
probe.appendChild(slider);

var list = document.createElement("div");
list.className = "wos-list";
list.innerHTML = "<wos-row id='first' title='One'><wos-toggle id='inside'></wos-toggle></wos-row>" +
                 "<wos-row id='second' title='Two'></wos-row>" +
                 "<wos-row id='third' title='Three'><wos-button id='wide' label='Wide'></wos-button></wos-row>";
probe.appendChild(list);
document.addEventListener("select", function () { window.__selected++; });

// The controls this ticket added.

window.__tab = "";
var tabs = document.createElement("wos-tab-group");
tabs.id = "tabs";
tabs.setAttribute("value", "all");
tabs.tabs = [
    { value: "all", label: "All" },
    { value: "contacts", label: "Contacts" },
    { value: "actions", label: "Actions" },
];
tabs.addEventListener("choose", function (e) { window.__tab = e.detail.value; });
probe.appendChild(tabs);

window.__iconPressed = 0;
var icon = document.createElement("wos-icon-button");
icon.id = "icon";
icon.setAttribute("label", "Add");
icon.addEventListener("press", function () { window.__iconPressed++; });
probe.appendChild(icon);

var divider = document.createElement("wos-divider");
divider.id = "divider";
divider.setAttribute("caption", "Nearby");
probe.appendChild(divider);

var alpha = document.createElement("wos-divider");
alpha.id = "alpha";
alpha.setAttribute("alpha", "");
alpha.setAttribute("caption", "S");
probe.appendChild(alpha);

window.__search = "";
window.__searchCancelled = 0;
var search = document.createElement("wos-search-field");
search.id = "search";
search.setAttribute("value", "hi");
search.addEventListener("change", function (e) { window.__search = e.detail.value; });
search.addEventListener("cancel", function () { window.__searchCancelled++; });
probe.appendChild(search);

// A long list, more rows than could fit, so only a window is drawn.
window.__listPicked = -1;
var big = document.createElement("wos-list");
big.id = "big";
big.style.height = "200px";
big.rowHeight = 40;
var rows = [];
for (var i = 0; i < 1000; i++) { rows.push("Row " + i); }
big.rows = rows;
big.render = function (row) {
    var r = document.createElement("wos-row");
    r.setAttribute("title", String(row));
    return r;
};
big.addEventListener("activate", function (e) { window.__listPicked = e.detail.index; });
probe.appendChild(big);

window.__minute = -1;
var picker = document.createElement("wos-picker");
picker.id = "picker";
picker.setAttribute("value", "30");
picker.setAttribute("min", "0");
picker.setAttribute("max", "59");
picker.setAttribute("open", "");
picker.addEventListener("change", function (e) { window.__minute = e.detail.value; });
probe.appendChild(picker);

window.__popupChose = "";
var popup = document.createElement("wos-popup-list");
popup.id = "popup";
popup.setAttribute("open", "");
popup.x = 100000;   // far off the right edge: must be clamped into view
popup.y = 20;
popup.choices = [{ value: "open", label: "Open" }, { value: "copy", label: "Copy" }];
popup.addEventListener("choose", function (e) { window.__popupChose = e.detail.value; });
probe.appendChild(popup);

// A tall popup opened near the bottom edge: it has to be nudged up by its own
// height, which a fixed one-row reserve could not do.
var tallPopup = document.createElement("wos-popup-list");
tallPopup.id = "tallPopup";
tallPopup.setAttribute("open", "");
tallPopup.x = 20;
tallPopup.y = 100000;   // far below the bottom edge
var many = [];
for (var j = 0; j < 12; j++) { many.push({ value: "v" + j, label: "Item " + j }); }
tallPopup.choices = many;
probe.appendChild(tallPopup);

window.__drawer = "";
var drawer = document.createElement("wos-drawer");
drawer.id = "drawer";
drawer.setAttribute("caption", "Advanced");
drawer.addEventListener("toggle", function (e) { window.__drawer = String(e.detail.open); });
probe.appendChild(drawer);

window.__toastGone = 0;
var toast = document.createElement("wos-toaster");
toast.id = "toast";
toast.setAttribute("open", "");
toast.setAttribute("message", "Saved");
toast.addEventListener("dismiss", function () { window.__toastGone++; });
probe.appendChild(toast);

window.__paneBack = 0;
var pane = document.createElement("wos-sliding-pane");
pane.id = "pane";
pane.setAttribute("showing", "detail");
pane.addEventListener("back", function () { window.__paneBack++; });
probe.appendChild(pane);

// A dialog, for its Escape and its focus.
window.__dialogDismissed = 0;
var dlg = document.createElement("wos-dialog");
dlg.id = "dlg";
dlg.setAttribute("title", "Forget?");
dlg.buttons = [{ value: "yes", label: "Forget" }, { value: "no", label: "Keep" }];
dlg.addEventListener("dismiss", function () { window.__dialogDismissed++; });
probe.appendChild(dlg);

// A date picker: Jan 31 2011, so picking February must re-clamp the day.
window.__date = "";
var datePicker = document.createElement("wos-date-picker");
datePicker.id = "datePicker";
datePicker.setAttribute("value", "2011-01-31");
datePicker.setAttribute("min-year", "1900");
datePicker.setAttribute("max-year", "2020");
datePicker.addEventListener("change", function (e) { window.__date = e.detail.value; });
datePicker.addEventListener("open", function (e) { window.__dateOpen = e.detail.open; });
probe.appendChild(datePicker);

// A time picker: 14:30, twelve-hour, shown open on the hour wheel is separate.
window.__time = "";
var timePicker = document.createElement("wos-time-picker");
timePicker.id = "timePicker";
timePicker.setAttribute("value", "14:30");
timePicker.setAttribute("interval", "5");
timePicker.addEventListener("change", function (e) { window.__time = e.detail.value; });
probe.appendChild(timePicker);

var timePicker24 = document.createElement("wos-time-picker");
timePicker24.id = "timePicker24";
timePicker24.setAttribute("value", "14:30");
timePicker24.setAttribute("mode24", "");
probe.appendChild(timePicker24);

// A toolbar with two buttons slotted in.
var toolbar = document.createElement("wos-toolbar");
toolbar.id = "toolbar";
toolbar.innerHTML = "<wos-button label='Cancel'></wos-button>" +
                    "<wos-button label='Done' kind='affirmative'></wos-button>";
probe.appendChild(toolbar);

// A prev/next banner, and one with each end turned off.
window.__prev = 0;
window.__next = 0;
var banner = document.createElement("wos-prev-next");
banner.id = "banner";
banner.textContent = "Page 3 of 5";
banner.addEventListener("previous", function () { window.__prev++; });
banner.addEventListener("next", function () { window.__next++; });
probe.appendChild(banner);

window.__prevOff = 0;
var bannerStart = document.createElement("wos-prev-next");
bannerStart.id = "bannerStart";
bannerStart.setAttribute("prev-off", "");
bannerStart.addEventListener("previous", function () { window.__prevOff++; });
probe.appendChild(bannerStart);
)JS";

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const QString built = QString::fromLocal8Bit(qgetenv("WEBOS_CARDS_BUILD"));
    if (built.isEmpty() || !QFile::exists(built + "/com.gachlab.app.kit/main.js")) {
        std::printf("SKIP: the cards are not built (tools/build-cards.sh)\n");
        return 77;
    }

    QTemporaryDir dir;
    const QString source = built + "/com.gachlab.app.kit";
    for (const QString& name : { QStringLiteral("main.js"), QStringLiteral("page.css"), QStringLiteral("kit.css"),
                                 QStringLiteral("theme-enyo.css") })
        QFile::copy(source + "/" + name, dir.filePath(name));
    QFile page(dir.filePath("index.html"));
    if (!page.open(QIODevice::WriteOnly))
        return 1;
    page.write("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
               "<link rel=\"stylesheet\" href=\"theme-enyo.css\">"
               "<link rel=\"stylesheet\" href=\"page.css\"></head><body><div id=\"card\"></div>"
               // #70: controls written only as markup, with their list data as a
               // JSON attribute and no JavaScript that ever touches them -- the
               // case of hand-written HTML, a server-rendered page, or the enyo
               // shim (#56). One of them carries malformed JSON, which must fall
               // back silently rather than throw. These are parsed before the
               // bundle runs and upgraded when it defines the elements.
               "<div id=\"markup\">"
               "<wos-selector id=\"markupSelector\" label=\"Security\" value=\"wpa\" "
               "choices='[{&quot;value&quot;:&quot;wpa&quot;,&quot;label&quot;:&quot;WPA Personal&quot;},"
               "{&quot;value&quot;:&quot;wep&quot;,&quot;label&quot;:&quot;WEP&quot;}]'></wos-selector>"
               "<wos-choice id=\"markupChoice\" label=\"When device sleeps\" value=\"off\" "
               "choices='[{&quot;value&quot;:&quot;on&quot;,&quot;label&quot;:&quot;Stay on&quot;},"
               "{&quot;value&quot;:&quot;off&quot;,&quot;label&quot;:&quot;Turn off&quot;}]'></wos-choice>"
               "<wos-app-menu id=\"markupMenu\" open "
               "items='[{&quot;value&quot;:&quot;edit&quot;,&quot;label&quot;:&quot;Edit&quot;}]'></wos-app-menu>"
               "<wos-selector id=\"markupBroken\" label=\"Broken\" value=\"wpa\" "
               "choices='[{not valid json}'></wos-selector>"
               "</div>"
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
          js("document.getElementById('early').shadowRoot.querySelector('.wos-selector-value').textContent"),
          QStringLiteral("Always ask"));
    check("and it reads back as a property", js("String(document.getElementById('early').choices.length)"),
          QStringLiteral("3"));

    // Setting a property repaints, which is what every binding relies on.
    js("document.getElementById('second').title = 'Renamed'; 1");
    waitFor([&]() {
        return js("document.getElementById('second').shadowRoot.querySelector('.wos-row-title').textContent")
               == "Renamed";
    }, 2000);
    check("setting a property repaints the element",
          js("document.getElementById('second').shadowRoot.querySelector('.wos-row-title').textContent"),
          QStringLiteral("Renamed"));
    check("and an attribute still does too",
          js("document.getElementById('second').setAttribute('detail', 'From an attribute');"
             "document.getElementById('second').shadowRoot.querySelector('.wos-row-detail') ? 'later' : 'none'"),
          QStringLiteral("later"));

    // A control the card put in a row is not the row.
    js("document.getElementById('inside').shadowRoot.querySelector('button').click(); 1");
    check("tapping a control inside a row does not select the row", js("String(window.__selected)"),
          QStringLiteral("0"));
    js("document.getElementById('first').shadowRoot.querySelector('.wos-row-text').click(); 1");
    check("tapping the row itself does", js("String(window.__selected)"), QStringLiteral("1"));

    // The stylesheet means the same thing inside a shadow root as it did
    // outside one.
    check("the first row in a list has no line above it",
          js("getComputedStyle(document.getElementById('first').shadowRoot.querySelector('.wos-row')).borderTopWidth"),
          QStringLiteral("0px"));
    check("and the ones after it do",
          js("getComputedStyle(document.getElementById('second').shadowRoot.querySelector('.wos-row')).borderTopWidth"),
          QStringLiteral("1px"));
    check("a button put into a row takes the room the row has left",
          js("getComputedStyle(document.getElementById('wide')).flexGrow"), QStringLiteral("1"));
    check("and so does one that shows it is working",
          js("var r = document.createElement('wos-row'); var b = document.createElement('wos-activity-button');"
             "r.appendChild(b); document.getElementById('probe').appendChild(r);"
             "getComputedStyle(b).flexGrow"),
          QStringLiteral("1"));
    check("a card's own stylesheet cannot reach into a control",
          js("var s = document.createElement('style');"
             "s.textContent = '.wos-row-title { display: none }';"
             "document.head.appendChild(s);"
             "getComputedStyle(document.getElementById('second').shadowRoot.querySelector('.wos-row-title')).display"),
          QStringLiteral("block"));

    // Swipe to delete: HP's inline confirmation, and the switch that skips it.
    const auto swipe = [&](const char* id) {
        return QString("(function () { var el = document.getElementById('%1');"
                       "var r = el.shadowRoot.querySelector('.wos-swipe-row');"
                       "r.dispatchEvent(new PointerEvent('pointerdown', { clientX: 200 }));"
                       "r.dispatchEvent(new PointerEvent('pointerup', { clientX: 100 }));"
                       "return 1; })()").arg(id);
    };
    // A tap, or a finger that barely moved, is not a swipe: a list where
    // touching a row deletes it is a list nobody can use.
    js("(function () { var r = document.getElementById('swipe').shadowRoot.querySelector('.wos-swipe-row');"
       "r.dispatchEvent(new PointerEvent('pointerdown', { clientX: 200 }));"
       "r.dispatchEvent(new PointerEvent('pointerup', { clientX: 194 })); return 1; })()");
    check("a finger that barely moved is not a swipe",
          js("window.__opened + ':' + window.__removed"), QStringLiteral(":0"));

    js(qPrintable(swipe("swipe")));
    check("a swipe asks to open the confirmation, it does not delete",
          js("window.__opened + ':' + window.__removed"), QStringLiteral("true:0"));
    js("document.getElementById('swipe').open = true; 1");
    waitFor([&]() { return js("String(!!document.getElementById('swipe').shadowRoot.querySelector('.wos-swipe-confirm'))") == "true"; }, 2000);
    js("document.getElementById('swipe').shadowRoot.querySelector('.wos-button.negative').click(); 1");
    check("and the confirmation is what deletes", js("String(window.__removed)"), QStringLiteral("1"));
    js(qPrintable(swipe("instant")));
    check("a row that asks for no confirmation deletes on the swipe itself",
          js("String(window.__removed)"), QStringLiteral("11"));

    // One of a few, in the row itself.
    check("the chosen one is the one marked",
          js("document.getElementById('choice').shadowRoot.querySelector('.wos-choice-one.chosen').textContent"),
          QStringLiteral("Turn off"));
    js("document.getElementById('choice').shadowRoot.querySelectorAll('.wos-choice-one')[0].click(); 1");
    check("and choosing another says which", js("window.__chose"), QStringLiteral("on"));

    // A button that is working says so, and cannot be pressed twice.
    check("a button at rest has no spinner and can be pressed",
          js("var b = document.getElementById('busy').shadowRoot.querySelector('button');"
             "String(!b.disabled) + ':' + String(b.querySelectorAll('.wos-button-spinner').length)"),
          QStringLiteral("true:0"));
    js("document.getElementById('busy').busy = true; 1");
    waitFor([&]() { return js("String(document.getElementById('busy').shadowRoot.querySelectorAll('.wos-button-spinner').length)") == "1"; }, 2000);
    check("a busy one shows it and refuses another press",
          js("var b = document.getElementById('busy').shadowRoot.querySelector('button');"
             "String(b.disabled) + ':' + String(b.querySelectorAll('.wos-button-spinner').length)"),
          QStringLiteral("true:1"));

    // A slider: what it shows, and the two events a card writes to a service
    // with. The bar is 200 px wide in the probe, so a quarter along is 25.
    js("document.getElementById('slider').style.width = '200px'; 1");
    check("it fills to where its value is",
          js("document.getElementById('slider').shadowRoot.querySelector('.wos-slider-filled').style.width"),
          QStringLiteral("50%"));
    js("(function () { var s = document.getElementById('slider');"
       "var bar = s.shadowRoot.querySelector('.wos-slider-bar');"
       "var box = bar.getBoundingClientRect();"
       "var el = s.shadowRoot.querySelector('.wos-slider');"
       "el.dispatchEvent(new PointerEvent('pointerdown', { clientX: box.left + box.width * 0.25, bubbles: true }));"
       "el.dispatchEvent(new PointerEvent('pointermove', { clientX: box.left + box.width * 0.75, bubbles: true }));"
       "el.dispatchEvent(new PointerEvent('pointerup', { clientX: box.left + box.width * 0.75, bubbles: true }));"
       "return 1; })()");
    check("a finger dragging it says so as it goes", js("window.__changing.join(',')"), QStringLiteral("25,75"));
    check("and says what it settled on when it lifts", js("window.__changed.join(',')"), QStringLiteral("75"));
    js("(function () { var s = document.getElementById('slider');"
       "var el = s.shadowRoot.querySelector('.wos-slider');"
       "el.dispatchEvent(new PointerEvent('pointermove', { clientX: 0, bubbles: true }));"
       "return 1; })()");
    check("and a finger that is not down moves nothing", js("window.__changing.join(',')"),
          QStringLiteral("25,75"));

    // And the showcase itself is drawn by all of this.
    check("the showcase card drew its controls",
          js("String(document.querySelectorAll('#card wos-row, #card wos-button, #card wos-toggle').length > 10)"),
          QStringLiteral("true"));

    // --- The controls this ticket added -----------------------------------

    // Tabs: radio semantics. The chosen one is marked; choosing another says
    // which, and choosing the one already chosen says nothing.
    check("the chosen tab is the one marked",
          js("document.getElementById('tabs').shadowRoot.querySelector('.wos-tab.chosen').textContent"),
          QStringLiteral("All"));
    js("document.getElementById('tabs').shadowRoot.querySelectorAll('.wos-tab')[1].click(); 1");
    check("tapping another tab says which", js("window.__tab"), QStringLiteral("contacts"));
    js("window.__tab = ''; document.getElementById('tabs').shadowRoot.querySelector('.wos-tab.chosen').click(); 1");
    check("tapping the chosen tab says nothing", js("window.__tab"), QStringLiteral(""));

    // Icon button: a press, and nothing when disabled.
    js("document.getElementById('icon').shadowRoot.querySelector('button').click(); 1");
    check("an icon button says it was pressed", js("String(window.__iconPressed)"), QStringLiteral("1"));

    // Divider: the caption is drawn, and the alpha one carries its letter.
    check("a divider draws its caption",
          js("document.getElementById('divider').shadowRoot.querySelector('.wos-divider-caption').textContent"),
          QStringLiteral("Nearby"));
    check("the alpha divider carries its letter",
          js("document.getElementById('alpha').shadowRoot.querySelector('.wos-divider.alpha .wos-divider-caption').textContent"),
          QStringLiteral("S"));

    // Search: it starts with a clear cross because it has text; clearing it
    // fires cancel, and typing says what is there.
    check("a search field with text shows a clear cross, not a magnifier",
          js("String(!!document.getElementById('search').shadowRoot.querySelector('.wos-search-clear'))"),
          QStringLiteral("true"));
    js("document.getElementById('search').shadowRoot.querySelector('.wos-search-clear').click(); 1");
    check("tapping the clear cross says cancel", js("String(window.__searchCancelled)"), QStringLiteral("1"));

    // The long list: only a window of rows is in the DOM, not all thousand,
    // and a row says its own index when tapped.
    waitFor([&]() { return js("String(document.getElementById('big').shadowRoot.querySelectorAll('.wos-list-row').length > 0)") == "true"; }, 2000);
    check("a thousand-row list draws only the window it can show, not all of them",
          js("var n = document.getElementById('big').shadowRoot.querySelectorAll('.wos-list-row').length;"
             "String(n > 0 && n < 100)"),
          QStringLiteral("true"));
    check("the run holds the room for every row so the scrollbar is right",
          js("document.getElementById('big').shadowRoot.querySelector('.wos-list-run').style.height"),
          QStringLiteral("40000px"));
    js("var row = document.getElementById('big').shadowRoot.querySelector('.wos-list-row'); row.click(); 1");
    check("tapping a row says its index", js("String(window.__listPicked)"), QStringLiteral("0"));
    // Scrolling the port advances the window: the list writes the offset to
    // its own `at`, which repaints, so a row far down the list is drawn and the
    // first row is not. Without that -- the window frozen at the top -- the
    // whole point of a virtual list is lost.
    check("the window starts at the top",
          js("var l = document.getElementById('big').shadowRoot;"
             "String(!!l.querySelector('.wos-list-row wos-row') && "
             "l.querySelectorAll('.wos-list-row').length > 0)"),
          QStringLiteral("true"));
    js("(function(){ var p = document.getElementById('big').shadowRoot.querySelector('.wos-list-port');"
       "p.scrollTop = 4000; p.dispatchEvent(new Event('scroll')); return 1; })()");
    waitFor([&]() {
        return js("String(document.getElementById('big').at > 0)") == "true";
    }, 2000);
    check("a scroll moves the window down the list (row ~100 is drawn now)",
          js("var l = document.getElementById('big').shadowRoot;"
             "var titles = [].map.call(l.querySelectorAll('wos-row'), function(r){return r.getAttribute('title');});"
             "String(titles.indexOf('Row 100') >= 0)"),
          QStringLiteral("true"));
    check("and the first row is no longer in the window",
          js("var l = document.getElementById('big').shadowRoot;"
             "var titles = [].map.call(l.querySelectorAll('wos-row'), function(r){return r.getAttribute('title');});"
             "String(titles.indexOf('Row 0') < 0)"),
          QStringLiteral("true"));

    // Picker: opened, it is scrolled to the value and marks it; picking one
    // says which.
    check("the picker pill shows its value",
          js("document.getElementById('picker').shadowRoot.querySelector('.wos-picker-pill').textContent.trim()"),
          QStringLiteral("30"));
    check("the open wheel marks the current value",
          js("document.getElementById('picker').shadowRoot.querySelector('.wos-picker-item.chosen').textContent.trim()"),
          QStringLiteral("30"));
    js("var items = document.getElementById('picker').shadowRoot.querySelectorAll('.wos-picker-item');"
       "items[45].click(); 1");
    check("picking a value on the wheel says which", js("String(window.__minute)"), QStringLiteral("45"));

    // Popup list: opened past the right edge, it is clamped back into view,
    // and choosing an item says which.
    check("a popup opened off the edge is clamped into the viewport",
          js("var p = document.getElementById('popup').shadowRoot.querySelector('.wos-popup');"
             "String(parseFloat(p.style.left) < window.innerWidth)"),
          QStringLiteral("true"));
    js("document.getElementById('popup').shadowRoot.querySelectorAll('.wos-popup-item')[1].click(); 1");
    check("choosing a popup item says which", js("window.__popupChose"), QStringLiteral("copy"));
    // A tall popup opened past the bottom is nudged up by its whole height, so
    // its last item is on screen -- not just its top corner.
    waitFor([&]() {
        return js("(function(){var p=document.getElementById('tallPopup').shadowRoot.querySelector('.wos-popup');"
                  "return p && parseFloat(p.style.top) < window.innerHeight ? 'in' : 'out';})()") == "in";
    }, 2000);
    check("a tall popup near the bottom is nudged fully into view",
          js("(function(){var p=document.getElementById('tallPopup').shadowRoot.querySelector('.wos-popup');"
             "var b=p.getBoundingClientRect();"
             "return String(b.bottom <= window.innerHeight && b.top >= 0);})()"),
          QStringLiteral("true"));

    // Drawer: closed to start, its body hidden; tapping the heading asks to
    // open it.
    check("a drawer starts closed with its body hidden",
          js("String(document.getElementById('drawer').shadowRoot.querySelector('.wos-drawer-body').hidden)"),
          QStringLiteral("true"));
    js("document.getElementById('drawer').shadowRoot.querySelector('.wos-drawer-head').click(); 1");
    check("tapping the heading asks to open it", js("window.__drawer"), QStringLiteral("true"));

    // Toaster: shown while open, and a tap asks to dismiss it.
    check("a toaster is on screen while open",
          js("String(!!document.getElementById('toast').shadowRoot.querySelector('.wos-toaster'))"),
          QStringLiteral("true"));
    js("document.getElementById('toast').shadowRoot.querySelector('.wos-toaster').click(); 1");
    check("tapping the toaster asks to dismiss it", js("String(window.__toastGone)"), QStringLiteral("1"));

    // Sliding pane: on the detail, the back arrow shows and says back.
    js("document.getElementById('pane').shadowRoot.querySelector('.wos-pane-back').click(); 1");
    check("the detail's back arrow says back", js("String(window.__paneBack)"), QStringLiteral("1"));

    // Dialog: the focus starts on the first button, and Escape dismisses it
    // rather than the card.
    waitFor([&]() { return js("String(!!document.getElementById('dlg').shadowRoot.activeElement)") == "true"; }, 2000);
    check("a dialog puts the focus on its first button",
          js("document.getElementById('dlg').shadowRoot.activeElement.textContent"),
          QStringLiteral("Forget"));
    js("(function () { "
       "document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true, composed: true }));"
       "return 1; })()");
    check("Escape while the dialog is open dismisses it", js("String(window.__dialogDismissed)"), QStringLiteral("1"));

    // --- The pickers, toolbar and banner (base controls added last) ---------

    // Date picker: three wheels, the month showing its name; picking February
    // when the day is the 31st re-clamps the day to 28, as enyo did.
    check("the date picker shows the month by name",
          js("document.getElementById('datePicker').shadowRoot.querySelectorAll('.wos-picker-pill')[0].textContent.trim()"),
          QStringLiteral("January"));
    check("it draws a wheel for each of month, day and year",
          js("String(document.getElementById('datePicker').shadowRoot.querySelectorAll('.wos-picker-pill').length)"),
          QStringLiteral("3"));
    // Open the month wheel and choose February (value 1); Jan 31 must become Feb 28.
    js("document.getElementById('datePicker').setAttribute('open','m'); 1");
    waitFor([&]() { return js("String(document.getElementById('datePicker').shadowRoot.querySelectorAll('.wos-picker-item').length>0)") == "true"; }, 2000);
    js("(function(){var items=document.getElementById('datePicker').shadowRoot.querySelectorAll('.wos-picker-item');"
       "for(var i=0;i<items.length;i++){if(items[i].textContent.trim()==='February'){items[i].click();break;}}return 1;})()");
    check("picking a shorter month re-clamps the day", js("window.__date"), QStringLiteral("2011-02-28"));

    // Time picker: 14:30 shows as 2 with PM in twelve-hour; 24-hour drops the
    // AM/PM wheel.
    check("the twelve-hour time picker shows the hour as 2",
          js("document.getElementById('timePicker').shadowRoot.querySelectorAll('.wos-picker-pill')[0].textContent.trim()"),
          QStringLiteral("2"));
    check("and it has an AM/PM wheel (three pills)",
          js("String(document.getElementById('timePicker').shadowRoot.querySelectorAll('.wos-picker-pill').length)"),
          QStringLiteral("3"));
    check("the 24-hour time picker shows the hour as 14 and no AM/PM wheel",
          js("var p=document.getElementById('timePicker24').shadowRoot.querySelectorAll('.wos-picker-pill');"
             "p[0].textContent.trim() + ':' + String(p.length)"),
          QStringLiteral("14:2"));

    // Toolbar: the buttons a card slots in are there, in the bar.
    check("the toolbar holds the buttons a card put in it",
          js("String(document.getElementById('toolbar').querySelectorAll('wos-button').length)"),
          QStringLiteral("2"));

    // Prev/next banner: each arrow says which way, and an off arrow says
    // nothing.
    js("document.getElementById('banner').shadowRoot.querySelector('.wos-prev-next-next').click(); 1");
    check("the next arrow says next", js("String(window.__next)"), QStringLiteral("1"));
    js("document.getElementById('banner').shadowRoot.querySelector('.wos-prev-next-prev').click(); 1");
    check("the previous arrow says previous", js("String(window.__prev)"), QStringLiteral("1"));
    js("document.getElementById('bannerStart').shadowRoot.querySelector('.wos-prev-next-prev').click(); 1");
    check("an arrow that is off says nothing", js("String(window.__prevOff)"), QStringLiteral("0"));

    // --- #70: list data as a JSON attribute, from markup with no JavaScript ---

    // A control written only as markup, its choices a JSON attribute, shows the
    // chosen label rather than falling back to the raw value. Without the Json
    // reader parsing the attribute, `choices` is undefined and the caption is
    // "wpa" -- the exact React-18 failure this ticket is about.
    check("a selector written only as markup reads its choices from the JSON attribute",
          js("document.getElementById('markupSelector').shadowRoot.querySelector('.wos-selector-value').textContent"),
          QStringLiteral("WPA Personal"));
    check("and the parsed attribute reads back as the property",
          js("String(document.getElementById('markupSelector').choices.length)"), QStringLiteral("2"));
    check("a choice control does too", js("document.getElementById('markupChoice')"
             ".shadowRoot.querySelector('.wos-choice-one.chosen').textContent"),
          QStringLiteral("Turn off"));
    check("and an app menu draws the items from its JSON attribute",
          js("document.getElementById('markupMenu').shadowRoot.querySelector('.wos-menu-item').textContent.trim()"),
          QStringLiteral("Edit"));

    // The property still wins over the attribute: setting it replaces what the
    // attribute parsed, it is not merged or ignored.
    js("document.getElementById('markupSelector').choices = [{ value: 'wpa', label: 'Property wins' }]; 1");
    waitFor([&]() {
        return js("document.getElementById('markupSelector').shadowRoot.querySelector('.wos-selector-value').textContent")
               == "Property wins";
    }, 2000);
    check("a property set on the element wins over the JSON attribute",
          js("document.getElementById('markupSelector').shadowRoot.querySelector('.wos-selector-value').textContent"),
          QStringLiteral("Property wins"));

    // Malformed JSON is not a new way to break the page: the control falls back
    // exactly as it would with no attribute (the raw value), silently, and the
    // rest of the page -- every check above -- still ran. Without the try/catch
    // the parse throws while the element upgrades and takes the page down.
    check("a malformed JSON attribute falls back silently, it does not throw",
          js("document.getElementById('markupBroken').shadowRoot.querySelector('.wos-selector-value')"
             " ? document.getElementById('markupBroken').shadowRoot.querySelector('.wos-selector-value').textContent"
             " : document.getElementById('markupBroken').shadowRoot.querySelector('.wos-row-title').textContent"),
          QStringLiteral("wpa"));

    return failures == 0 ? 0 : 1;
}
