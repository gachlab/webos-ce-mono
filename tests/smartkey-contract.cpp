// What com.palm.smartKey answers, checked without the bus or a dictionary.
//
// smartkey_contract.h shapes every reply the keyboard and the browser read, and
// decodes the keyboard's packed taps and trace, free of both the Luna bus and
// hunspell. This drives it against a fake SmartKey::Engine holding a fixed word
// list, so the contract is pinned without ls-hubd and without a .dic on disk --
// the delicate half being processTaps, which CandidateBarRemote reads field by
// field and discards entirely if "traceEntry" is missing. The hunspell half
// (resolving a locale to a dictionary, spell/suggest/add) is exercised
// separately in hunspell-engine. (#34)

#include "smartkey_contract.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

static int g_failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("  %-70s %s\n", what, ok ? "OK" : "<-- FAIL");
    if (!ok)
        ++g_failures;
}

static bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

// A dictionary with a fixed vocabulary and fixed corrections, so the shaping is
// what is under test and not hunspell's choices. "known" words spell true;
// "corrections" maps a misspelling to the ordered suggestions it should return.
class FakeEngine : public SmartKey::Engine {
public:
    std::vector<std::string> known;
    std::map<std::string, std::vector<std::string>> corrections;
    std::vector<std::string> learned;

    bool spelled(const std::string& word) const override
    {
        return std::find(known.begin(), known.end(), word) != known.end();
    }
    std::vector<std::string> suggest(const std::string& word) const override
    {
        auto it = corrections.find(word);
        return it == corrections.end() ? std::vector<std::string>{} : it->second;
    }
    bool learn(const std::string& word) override
    {
        learned.push_back(word);
        return true;
    }
};

// A tap carrying one letter key, the way CandidateBarRemote builds them.
static SmartKey::Tap letterTap(char c)
{
    SmartKey::Tap t;
    t.key = static_cast<int>(c);
    return t;
}

static std::vector<SmartKey::Tap> tapsFor(const std::string& word)
{
    std::vector<SmartKey::Tap> taps;
    for (char c : word)
        taps.push_back(letterTap(c));
    return taps;
}

int main()
{
    // --- the taps decode to the word being typed -----------------------------
    std::printf("taps decode to a word, dropping anything that is not a letter or digit\n");
    {
        std::vector<SmartKey::Tap> taps = tapsFor("he");
        // A space and a Qt arrow key in the middle must not become characters.
        SmartKey::Tap space; space.key = ' ';
        SmartKey::Tap arrow; arrow.key = 0x01000012; // Qt::Key_Left
        taps.push_back(space);
        taps.push_back(arrow);
        for (char c : std::string("llo"))
            taps.push_back(letterTap(c));
        check(SmartKey::wordFromTaps(taps) == "hello", "'he', space, arrow, 'llo' -> 'hello'");

        // An uppercase key lowercases: the word is spell-checked, case is the
        // keyboard's own shift state.
        check(SmartKey::wordFromTaps(tapsFor("HELLO")) == "hello", "uppercase keys lowercase to the word");
    }

    // --- a trace unpacks both coordinates from one int32 ----------------------
    std::printf("a trace point unpacks x and y from a single packed int32\n");
    {
        const int32_t packed = ((123 & 0xFFFF) << 16) | (456 & 0xFFFF);
        SmartKey::TracePoint p = SmartKey::unpackTracePoint(packed);
        check(p.x == 123 && p.y == 456, "((123<<16)|456) -> x=123 y=456");
    }

    // --- processTaps: a correctly spelled word is left alone ------------------
    std::printf("a correctly spelled word: spelledCorrectly true, no autocorrection\n");
    {
        FakeEngine engine;
        engine.known = {"hello"};
        const std::string reply = SmartKey::answerTaps(engine, tapsFor("hello"));
        // traceEntry MUST be present or CandidateBarRemote discards the reply.
        check(contains(reply, "\"traceEntry\":false"), "traceEntry is present (false for taps)");
        check(contains(reply, "\"spelledCorrectly\":true"), "spelledCorrectly is true");
        check(contains(reply, "\"str\":\"hello\""), "the typed word is a guess");
        // A correctly spelled word is never auto-accepted: autocorrecting a real
        // word is the thing that makes autocorrect infuriating.
        check(!contains(reply, "auto-accept"), "nothing is auto-accepted for a correct word");
    }

    // --- processTaps: a correct word with suggestions available still not fixed
    std::printf("a correct word is not auto-accepted even when corrections exist\n");
    {
        // "to" is a real word, but a dictionary may still suggest "too"/"two".
        // The auto-accept guard keys off whether the TYPED word is correct, so
        // these suggestions must not become an autocorrection. This is the case
        // that a mutation removing the !typedOk guard breaks.
        FakeEngine engine;
        engine.known = {"to", "too", "two"};
        engine.corrections = { {"to", {"too", "two"}} };
        const std::string reply = SmartKey::answerTaps(engine, tapsFor("to"));
        check(contains(reply, "\"spelledCorrectly\":true"), "the typed word is correct");
        check(contains(reply, "\"str\":\"too\""), "corrections are still offered as candidates");
        check(!contains(reply, "auto-accept"), "but none is auto-accepted, because the typed word is right");
    }

    // --- processTaps: a misspelling autocorrects to its top correction --------
    std::printf("a misspelling: typed word first, correction second and auto-accepted\n");
    {
        FakeEngine engine;
        engine.known = {"hello", "help"};
        engine.corrections = { {"helllo", {"hello", "help"}} };
        const std::string reply = SmartKey::answerTaps(engine, tapsFor("helllo"));
        check(contains(reply, "\"spelledCorrectly\":false"), "spelledCorrectly is false");

        const size_t typedAt = reply.find("\"str\":\"helllo\"");
        const size_t fixAt = reply.find("\"str\":\"hello\"");
        // guesses[0] is the word as typed, guesses[1..] the corrections: the
        // keyboard falls back to guesses[1] when the typed word is wrong.
        check(typedAt != std::string::npos && fixAt != std::string::npos && typedAt < fixAt,
              "the typed word comes before the correction");
        // The correction, not the typed word, carries auto-accept so the
        // composing line shows the fix.
        const size_t autoAt = reply.find("auto-accept");
        check(autoAt != std::string::npos && autoAt > fixAt,
              "auto-accept is on the correction, not the typed word");
        // ONLY the first correction is auto-accepted: a mutation that dropped
        // the guesses.size()==1 guard would mark every correction, so there must
        // be exactly one auto-accept and "help" (the second) must not carry it.
        check(reply.find("auto-accept", autoAt + 1) == std::string::npos,
              "exactly one guess is auto-accepted, not every correction");
        const size_t helpAt = reply.find("\"str\":\"help\"");
        check(helpAt != std::string::npos && autoAt < helpAt,
              "the second correction (help) comes after the one auto-accept");
    }

    // --- processTaps: a trace keeps traceEntry true and never auto-accepts -----
    std::printf("a trace answers with traceEntry true and never silently replaces\n");
    {
        FakeEngine engine;
        engine.known = {"go"};
        engine.corrections = { {"go", {"got", "gone"}} };
        const std::string reply = SmartKey::answerTrace(engine, "g", "o");
        check(contains(reply, "\"traceEntry\":true"), "traceEntry is true for a trace");
        check(!contains(reply, "auto-accept"), "a swipe never auto-accepts");
    }

    // --- processTaps: an empty request is still a well-formed clearing reply ---
    std::printf("an empty tap list clears the bar with a well-formed reply\n");
    {
        FakeEngine engine;
        const std::string reply = SmartKey::answerTaps(engine, {});
        check(contains(reply, "\"traceEntry\":false"), "traceEntry present so the reply is not discarded");
        check(contains(reply, "\"guesses\":[]"), "no guesses");
    }

    // --- processTaps: a trace with empty endpoints clears, still traceEntry ---
    std::printf("a trace with no endpoints clears the bar but stays a trace reply\n");
    {
        FakeEngine engine;
        const std::string reply = SmartKey::answerTrace(engine, "", "");
        check(contains(reply, "\"traceEntry\":true"), "traceEntry is true even when empty");
        check(contains(reply, "\"guesses\":[]"), "no guesses from an empty trace");
    }

    // --- search: the browser's single best completion -------------------------
    std::printf("search returns the query itself when spelled, else its top correction\n");
    {
        FakeEngine engine;
        engine.known = {"hello"};
        engine.corrections = { {"helllo", {"hello", "help"}}, {"zzz", {}} };

        const std::string ok = SmartKey::searchReply(engine, "hello");
        check(contains(ok, "\"returnValue\":true") && contains(ok, "\"match\":\"hello\""),
              "a spelled query matches itself");

        const std::string fix = SmartKey::searchReply(engine, "helllo");
        check(contains(fix, "\"returnValue\":true") && contains(fix, "\"match\":\"hello\""),
              "a misspelling matches its top correction");

        const std::string none = SmartKey::searchReply(engine, "zzz");
        check(contains(none, "\"returnValue\":false") && !contains(none, "\"match\""),
              "a word with no correction returns no match");

        const std::string empty = SmartKey::searchReply(engine, "");
        check(contains(empty, "\"returnValue\":false"), "an empty query is not a match");
    }

    // --- learn: a word is added, an empty word is refused ---------------------
    std::printf("learn adds a non-empty word and refuses an empty one\n");
    {
        FakeEngine engine;
        const std::string ok = SmartKey::learnReply(engine, "webOS");
        check(contains(ok, "\"returnValue\":true"), "a word is learned");
        check(engine.learned.size() == 1 && engine.learned[0] == "webOS",
              "the engine was asked to learn it");

        const std::string bad = SmartKey::learnReply(engine, "");
        check(contains(bad, "\"returnValue\":false"), "an empty word is a bad request");
        check(engine.learned.size() == 1, "nothing was learned from the empty word");
    }

    // --- JSON escaping: a quote in a word does not break the reply ------------
    std::printf("a candidate with a quote or backslash is escaped\n");
    {
        check(SmartKey::jsonEscape("a\"b\\c") == "a\\\"b\\\\c", "quote and backslash are escaped");
    }

    std::printf("\n%s\n", g_failures ? "FAILED" : "all good");
    return g_failures ? 1 : 0;
}
