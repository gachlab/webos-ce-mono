// A page WebAppMgr focuses has the focus, so its fields hear about it.
//
// WebAppMgr tells a page its window was activated with a QFocusEvent. The
// compat layer dropped it, so no page ever had the focus: document.hasFocus()
// was false in every card, and a field's focus event never fired. Just Type
// clears its "Just type..." hint in that event, so the hint stayed and what
// the user typed went in front of it -- "TESTRJust type...".
//
// Verified by mutation: without forwarding FocusIn, or FocusOut, or without
// taking the focus on input, this turns red.

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFocusEvent>
#include <QKeyEvent>

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
    std::printf("%-58s %-10s %s\n", what, qPrintable(got), ok ? "ok" : qPrintable("FAILED, wanted " + expected));
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QWebPage page;
    bool loaded = false;
    QObject::connect(&page, &QWebPage::loadFinished, &page, [&loaded]() { loaded = true; });
    page.mainFrame()->setHtml(QStringLiteral(
        "<html><body><div id='field' contenteditable='true'>Just type...</div><script>"
        "window.__focus = 0; window.__blur = 0;"
        "var f = document.getElementById('field');"
        "f.addEventListener('focus', function () { window.__focus++; });"
        "f.addEventListener('blur', function () { window.__blur++; });"
        "</script></body></html>"));
    if (!waitFor([&]() { return loaded; }, 15000))
        return 1;
    const auto js = [&](const char* code) { return page.mainFrame()->evaluateJavaScript(code).toString(); };

    // What Just Type does when it is shown: focus its field.
    js("document.getElementById('field').focus(); 1");

    QFocusEvent in(QEvent::FocusIn);
    page.event(&in);
    waitFor([&]() { return js("String(document.hasFocus())") == "true"; }, 5000);
    check("a focused page has the focus", js("String(document.hasFocus())"), "true");
    waitFor([&]() { return js("String(window.__focus)") != "0"; }, 5000);
    check("and its focused field hears about it", js("String(window.__focus)"), "1");

    QFocusEvent out(QEvent::FocusOut);
    page.event(&out);
    waitFor([&]() { return js("String(document.hasFocus())") == "false"; }, 5000);
    check("an unfocused page no longer has it", js("String(document.hasFocus())"), "false");
    waitFor([&]() { return js("String(window.__blur)") != "0"; }, 5000);
    check("and the field hears that too", js("String(window.__blur)"), "1");

    // A window activated before its page was loaded -- Just Type's launcher,
    // which WebAppMgr focuses at startup -- and then typed into: the page
    // takes the focus again, so the field's focus handler -- here, like
    // enyo's, clearing a hint -- runs before the key lands.
    QWebPage typed;
    QFocusEvent early(QEvent::FocusIn, Qt::OtherFocusReason);
    typed.event(&early);
    loaded = false;
    QObject::connect(&typed, &QWebPage::loadFinished, &typed, [&loaded]() { loaded = true; });
    typed.mainFrame()->setHtml(QStringLiteral(
        "<html><body><div id='field' contenteditable='true'>Just type...</div><script>"
        "var f = document.getElementById('field');"
        "f.addEventListener('focus', function () {"
        "  if (f.innerText === 'Just type...') f.innerText = '';"
        "});"
        "</script></body></html>"));
    if (!waitFor([&]() { return loaded; }, 15000))
        return 1;
    const auto typedJs = [&](const char* code) { return typed.mainFrame()->evaluateJavaScript(code).toString(); };
    typedJs("document.getElementById('field').focus(); 1");
    QKeyEvent press(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a");
    typed.event(&press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier, "a");
    typed.event(&release);
    waitFor([&]() { return typedJs("document.getElementById('field').innerText") == "a"; }, 5000);
    check("typing into an unfocused page clears the hint first",
          typedJs("document.getElementById('field').innerText"), "a");
    check("and leaves the page focused", typedJs("String(document.hasFocus())"), "true");

    return failures == 0 ? 0 : 1;
}
