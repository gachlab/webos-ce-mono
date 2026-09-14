// Which webOS key codes have to be rewritten before a web engine sees them,
// and -- more important -- which ones must be left alone.
//
// The back gesture posts webOS Key_CoreNavi_Back, which is that catalogue's
// Key_Escape = 0x1B. Qt's Key_Escape is 0x01000000. Under QtWebKit the
// difference never showed, because every code went through
// WebKitKeyMap::translateKey before reaching WebCore; that branch is commented
// out in this port and the QKeyEvent goes to Chromium unchanged.
//
// MEASURED in the running browser, listener on the app document, 13 gestures:
//     KEYPROBE keydown keyCode=0 which=0 key="Escape" target=INPUT#input-29
// The name survives, the number does not, and enyo's Gesture.js tests the
// number alone (`if (e.keyCode == 27)`), so no "back" is ever synthesised. The
// same key injected with keyCode 27 through DevTools does synthesise it.
//
// The risk in fixing that is not the escape, it is everything else: this runs
// on every key a card receives, so a translation that answers too eagerly would
// rewrite ordinary typing. That is what most of this file pins down.
//
// Runs headless:  ./key-translate -platform offscreen
#include <cstdio>
#include <cstring>

#include <webos_keys.h>

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

int main()
{
    // The one code that is wrong, and the whole reason this exists.
    const WebosKeys::QtKey back = WebosKeys::toQtKey(WebosKeys::kBack);
    check(back.isTranslated(), "the back key must be translated");
    check(back.key == Qt::Key_Escape,
          "back must become Qt::Key_Escape, the only value Chromium turns into DOM keyCode 27");
    check(back.key != 0x1B,
          "back must NOT stay 0x1B -- that is the webOS catalogue's escape and it is what produced keyCode=0");

    // The text is half the translation. Chromium builds the character from it,
    // and an empty one on a key that has a character is how a keypress goes
    // missing in a page that reads e.key or e.char.
    check(back.text != nullptr, "text is never null");
    check(std::strcmp(back.text, "\x1b") == 0, "escape carries its own character");

    // Everything else must be refused. A letter already arrives as a valid Qt
    // key; rewriting it would break typing in order to fix a gesture.
    check(!WebosKeys::toQtKey(Qt::Key_A).isTranslated(), "a letter is left alone");
    check(!WebosKeys::toQtKey(Qt::Key_Space).isTranslated(), "space is left alone");
    check(!WebosKeys::toQtKey(Qt::Key_Return).isTranslated(), "return is left alone");
    check(!WebosKeys::toQtKey(Qt::Key_Escape).isTranslated(),
          "a Qt escape is already right and must not be translated again");
    check(!WebosKeys::toQtKey(0).isTranslated(), "zero is not a key");
    check(!WebosKeys::toQtKey(-1).isTranslated(), "Key_Invalid is not a key");

    // The gesture keys the strip also posts. They reach a card the same way,
    // and they have no Qt equivalent: webOS gave them to apps as Mojo gestures,
    // through a path that is commented out here. Answering them with some
    // arbitrary Qt key would type into pages that never asked for it.
    check(!WebosKeys::toQtKey(WebosKeys::kMenu).isTranslated(), "menu is not turned into a keystroke");
    check(!WebosKeys::toQtKey(WebosKeys::kPrevious).isTranslated(), "previous is not turned into a keystroke");
    check(!WebosKeys::toQtKey(WebosKeys::kNext).isTranslated(), "next is not turned into a keystroke");

    // An untranslated answer must be safe to use without checking the key: the
    // caller reads text() unconditionally when building the replacement event.
    check(WebosKeys::toQtKey(Qt::Key_A).text != nullptr, "text is never null, translated or not");

    if (g_failures == 0)
        std::printf("key-translate: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
