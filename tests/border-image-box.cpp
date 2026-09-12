// -webkit-border-image used to imply a border box, and the apps count on it.
//
// In the WebKit webOS shipped, border-width applied to an element with a border
// image even with no border-style declared; Chromium computes it to 0. The
// stylesheets in this tree never declare border-style, so every control drawn
// through a border image lost its inset -- and apps that measure their own
// elements to size themselves (the calculator picks its font from the key it
// measures) laid out wrong because of it.
//
// The compat layer injects a script that puts the border back. This checks the
// three cases that matter: a rule with a border image gets the border box, a
// rule without one is left alone, and an author's own border-style is not
// overridden.

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

static void check(const char* what, int got, int expected)
{
    const bool ok = got == expected;
    if (!ok)
        ++failures;
    std::printf("%-46s %4d (expected %4d)  %s\n", what, got, expected, ok ? "ok" : "FAILED");
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QWebPage page;
    bool loaded = false;
    QObject::connect(&page, &QWebPage::loadFinished, &page, [&](bool) { loaded = true; });

    // A 1x1 red PNG, so nothing is fetched from anywhere.
    const QString image = QStringLiteral(
        "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==");

    page.mainFrame()->setHtml(QStringLiteral(
        "<html><head>"
        "<script>window.__resizes = 0;"
        "        window.addEventListener('resize', function () { window.__resizes++; });</script>"
        "<style>"
        "  div { width: 100px; height: 100px; }"
        "  .framed   { border-width: 15px; -webkit-border-image: url(%1) 1 1 1 1 stretch stretch; }"
        "  .plain    { border-width: 15px; }"
        "  .explicit { border-width: 15px; border-style: dashed;"
        "              -webkit-border-image: url(%1) 1 1 1 1 stretch stretch; }"
        // Inside a grouping rule, which is where enyo keeps the whole radio and
        // tab button theme -- @media (-webkit-max-device-pixel-ratio: ...).
        // A walk over sheet.cssRules never reaches these: a CSSMediaRule has no
        // .style, so a loop that skips on that alone skips the lot. The clock's
        // toolbar buttons collapsed onto their bare icons because of it.
        "  @media screen {"
        "    .inmedia { border-width: 15px;"
        "               -webkit-border-image: url(%1) 1 1 1 1 stretch stretch; }"
        "  }"
        "</style></head><body style='margin:0'>"
        "<div id='framed' class='framed'></div>"
        "<div id='plain' class='plain'></div>"
        "<div id='explicit' class='explicit'></div>"
        "<div id='inmedia' class='inmedia'></div>"
        "</body></html>").arg(image));

    if (!waitFor([&]() { return loaded; }, 15000)) {
        std::printf("the page never finished loading\n");
        return 1;
    }
    // The script patches the sheets on DOMContentLoaded and again on load.
    QEventLoop settle;
    QTimer::singleShot(500, &settle, &QEventLoop::quit);
    settle.exec();

    const auto width = [&](const char* id) {
        return page.mainFrame()
            ->evaluateJavaScript(QStringLiteral("document.getElementById('%1').offsetWidth").arg(id))
            .toInt();
    };
    const auto borderStyle = [&](const char* id) {
        return page.mainFrame()
            ->evaluateJavaScript(QStringLiteral(
                "getComputedStyle(document.getElementById('%1')).borderLeftStyle").arg(id))
            .toString();
    };

    // 100 of content plus 15 of border on each side: the box the app expects.
    check("element with a border image", width("framed"), 130);
    // border-width with no image is not ours to change; Chromium drops it.
    check("element without one", width("plain"), 100);
    // An author who did declare a style keeps it.
    check("element with its own border-style", width("explicit"), 130);
    // The same, inside @media. Without recursing into grouping rules this is
    // 100 -- indistinguishable from having no border image at all, which is
    // exactly how the clock's toolbar rendered while this test still passed.
    check("element inside @media", width("inmedia"), 130);

    // Restoring the border shrinks every one of these boxes, and an app that
    // already measured itself has to be told. Without this the calculator kept
    // the 84px font it had computed for a key that was no longer that size.
    const int resizes = page.mainFrame()
                            ->evaluateJavaScript(QStringLiteral("window.__resizes"))
                            .toInt();
    if (resizes < 1) {
        ++failures;
        std::printf("%-46s %4d (expected >= 1)  FAILED\n", "a resize follows, so the page measures again", resizes);
    } else {
        std::printf("%-46s %4d  ok\n", "a resize follows, so the page measures again", resizes);
    }

    const QString explicitStyle = borderStyle("explicit");
    if (explicitStyle != QStringLiteral("dashed")) {
        ++failures;
        std::printf("%-46s %s (expected dashed)  FAILED\n", "its border-style is left alone",
                    qPrintable(explicitStyle));
    } else {
        std::printf("%-46s %s  ok\n", "its border-style is left alone", qPrintable(explicitStyle));
    }

    return failures == 0 ? 0 : 1;
}
