// Number inputs keep the text they are given, as in webOS's WebKit.
//
// HP's Wi-Fi settings write addresses such as "10.20.30.99" into
// <input type="number"> fields. Chromium drops any value that is not a
// floating-point number, so the connected network's address showed as an empty
// field. The compat layer turns such an input into a text input with a numeric
// keyboard hint; this checks it against a page shaped like those fields, set
// the way enyo sets them -- in the same task that rendered them.

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

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
    std::printf("%-56s %-22s %s\n", what, qPrintable(got), ok ? "ok" : qPrintable("FAILED, wanted " + expected));
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QWebPage page;
    bool loaded = false;
    QObject::connect(&page, &QWebPage::loadFinished, &page, [&](bool) { loaded = true; });
    page.mainFrame()->setHtml(QStringLiteral(
        "<html><body><div id='box'></div><script>"
        // Rendered and filled in one go, as enyo's Input does.
        "  var box = document.getElementById('box');"
        "  box.innerHTML = \"<input id='ip' type='number'><input id='port' type='number'>\""
        "                + \"<input id='typed' type='number'><input id='name' type='text'>\";"
        "  document.getElementById('ip').value = '10.20.30.99';"
        "  document.getElementById('port').value = '8080';"
        "</script></body></html>"));
    if (!waitFor([&]() { return loaded; }, 15000)) {
        std::printf("the page never finished loading\n");
        return 1;
    }

    const auto js = [&](const QString& code) {
        return page.mainFrame()->evaluateJavaScript(code).toString();
    };

    check("an address set on a number input is kept",
          js("document.getElementById('ip').value"), "10.20.30.99");
    check("that input is now text", js("document.getElementById('ip').type"), "text");
    check("with the numeric keyboard hint", js("document.getElementById('ip').getAttribute('inputmode')"), "decimal");

    check("a real number is kept as well", js("document.getElementById('port').value"), "8080");
    check("and that input is left a number input", js("document.getElementById('port').type"), "number");

    // Dispatched rather than focus(): an offscreen page never has the focus, so
    // Chromium does not fire focus events for it.
    js("document.getElementById('typed').dispatchEvent(new FocusEvent('focusin', {bubbles: true}))");
    check("an input the user focuses becomes text before typing",
          js("document.getElementById('typed').type"), "text");
    js("document.getElementById('typed').value = '192.168.1.1'");
    check("so what is typed there reads back", js("document.getElementById('typed').value"), "192.168.1.1");

    check("a text input is left alone", js("document.getElementById('name').getAttribute('inputmode')"), "");
    js("document.getElementById('name').value = 'a.b.c'");
    check("and still takes its value", js("document.getElementById('name').value"), "a.b.c");

    return failures == 0 ? 0 : 1;
}
