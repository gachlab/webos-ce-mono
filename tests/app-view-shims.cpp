// The mail app calls WebView methods on a view that is not a WebView.
//
// MessageDisplay puts the message body in "DivHtmlView" -- plain DOM; the
// WebView-backed body beside it is commented out in HP's source. Its rendered()
// still calls setRedirects() on that body if PalmSystem is present, which it is
// here, so the call threw and aborted _unhideMainApp() on its first line: the
// mail card never selected its mail view and painted it under the first-launch
// screen.
//
// The compat layer adds the missing side of that interface to the kind. This
// checks it against a stand-in with the same shape as the app's: the methods
// appear, setHTML goes through the view's own sanitise-and-show path, and a kind
// that already has its own implementations keeps them.

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
    std::printf("%-52s %-22s %s\n", what, qPrintable(got), ok ? "ok" : qPrintable("FAILED, wanted " + expected));
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QWebPage page;
    bool loaded = false;
    QObject::connect(&page, &QWebPage::loadFinished, &page, [&](bool) { loaded = true; });

    // A stand-in shaped like the app's kind: a wrapper that records what it is
    // given, and the sanitiser the real one runs a body through.
    page.mainFrame()->setHtml(QStringLiteral(
        "<html><body><script>"
        // enyo.kind finishes the constructor and its prototype and only then
        // publishes it under its name (Oop.js, through enyo.setObject), which is
        // the moment the shim patches. The stand-in has to be built in that
        // order: assigning the prototype afterwards would throw the shim's
        // methods away, which is a shape no kind in this tree has.
        "  var Div = function () {};"
        "  Div.prototype = {"
        "    loadedAndSanitize: function (html) { return '[clean]' + html; },"
        "    fitWidth: function () { window.__fitted = true; },"
        "    doViewReady: function () { window.__ready = true; }"
        "  };"
        "  window.DivHtmlView = Div;"
        "  window.Untouched = function () {};"
        "  window.Untouched.prototype = { setRedirects: function () { return 'mine'; } };"
        "</script></body></html>"));

    if (!waitFor([&]() { return loaded; }, 15000)) {
        std::printf("the page never finished loading\n");
        return 1;
    }

    const auto js = [&](const QString& code) {
        return page.mainFrame()->evaluateJavaScript(code).toString();
    };

    // The shim polls for the kind, which the page defines after it runs.
    if (!waitFor([&]() { return js("typeof DivHtmlView.prototype.setRedirects") == "function"; }, 10000))
        std::printf("note: the shim never reached the kind\n");

    check("setRedirects is there for the app to call",
          js("typeof DivHtmlView.prototype.setRedirects"), "function");
    check("so is cancelDialog", js("typeof DivHtmlView.prototype.cancelDialog"), "function");
    check("so is setHTML", js("typeof DivHtmlView.prototype.setHTML"), "function");

    // setHTML must go through the view's own path, not around it.
    js("window.__content = null;"
       "var v = new DivHtmlView();"
       "v.$ = { wrapper: { setContent: function (c) { window.__content = c; } } };"
       "v.setHTML('file:///message.html', '<p>hello</p>');");
    check("setHTML sanitises through the view", js("String(window.__content)"), "[clean]<p>hello</p>");
    check("and lets the view lay itself out", js("String(!!window.__fitted)"), "true");
    check("and reports it is ready", js("String(!!window.__ready)"), "true");

    // A kind that has its own implementation must keep it.
    check("an existing implementation is left alone",
          js("(new Untouched()).setRedirects()"), "mine");

    return failures == 0 ? 0 : 1;
}
