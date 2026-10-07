/* @@@LICENSE
 *
 * Copyright (c) 2026 webOS CE modern build
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * LICENSE@@@ */

#ifndef SMARTKEY_CONTRACT_H
#define SMARTKEY_CONTRACT_H

//
// What com.palm.smartKey answers, kept here and free of both the Luna bus and
// hunspell so tests/smartkey-contract.cpp can pin the exact payloads without
// ls-hubd and without a dictionary on disk. Same shape as network_state.h,
// print_state.h and share_replies.h.
//
// HP never released com.palm.smartKey as source. It was the spelling and
// word-completion service behind the keyboard's candidate bar and the browser's
// text fields; this is ours, answering the same bus names from hunspell (the
// open-source checker HP itself used). The three methods are the only ones any
// client in the tree calls, measured rather than assumed:
//
//   processTaps   components/keyboard-efigs/src/CandidateBarRemote.cpp:236
//                 the on-screen keyboard, for suggestions and autocorrection
//   search        components/BrowserServer/Src/BrowserPage.cpp:2859
//                 the browser's text fields, for a single best completion
//   learn         components/BrowserServer/Src/BrowserPage.cpp:2832
//                 add a word to the user dictionary; fire-and-forget
//
// This header is the translation between a hunspell result -- "is this a word?"
// and "what did you mean?" -- and the JSON each caller parses. The engine that
// answers those two questions lives in hunspell_engine.h, behind an interface
// this header takes by reference, so the shaping is checked with a fake engine
// holding a fixed word list and the real one is exercised separately.
//
// The reply of processTaps is the delicate half, because CandidateBarRemote
// reads it field by field and discards the whole thing if "traceEntry" is
// missing (CandidateBarRemote.cpp:138). The guesses it shows are ordered by a
// convention this header has to honour: guesses[0] is the word as typed,
// guesses[1..] are corrections, and the keyboard's choice of what to put in the
// composing line is:
//
//   - the first guess carrying "auto-accept":true, if any; else
//   - guesses[0] when spelledCorrectly; else
//   - guesses[1] when there are two or more.
//
// so a misspelling has to produce at least two guesses (the typed word, then
// the top correction) or the keyboard autocorrects to nothing.
//

#include <cstdint>
#include <string>
#include <vector>

namespace SmartKey {

// --- the word engine, as this header needs it --------------------------------

// The two questions the contract asks of a dictionary, and the one change it
// makes. hunspell_engine.h implements this against libhunspell; a test
// implements it against a fixed set of words. Nothing here knows which.
struct Engine {
    virtual ~Engine() = default;
    // Is this a correctly spelled word?
    virtual bool spelled(const std::string& word) const = 0;
    // Corrections, best first, for a word (correct or not).
    virtual std::vector<std::string> suggest(const std::string& word) const = 0;
    // Remember a word as correct from now on. Returns false if it could not.
    virtual bool learn(const std::string& word) = 0;
};

// --- a single candidate, as the keyboard reads it ----------------------------

struct Guess {
    std::string str;         // the candidate word
    bool spelled = false;    // "sp": is it a dictionary word?
    bool autoAccept = false; // "auto-accept": replace the typed word with this
};

// --- JSON helpers, local so the header stays dependency-free -----------------

inline std::string jsonEscape(const std::string& in)
{
    std::string out;
    out.reserve(in.size() + 2);
    for (char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0xF];
                    out += hex[c & 0xF];
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// --- processTaps: decoding the request ---------------------------------------

// A tap is x, y, a key code and whether shift was down. The keyboard sends a
// flat array [x0,y0,char0,shift0, x1,y1,char1,shift1, ...]; this is one group.
struct Tap {
    int32_t x = 0;
    int32_t y = 0;
    int32_t key = 0;   // a Qt::Key code, not necessarily a clean character
    bool shift = false;
};

// A trace (swipe) carries both coordinates packed into one int32 to halve the
// JSON: ((x & 0xFFFF) << 16) | (y & 0xFFFF). This unpacks one point.
struct TracePoint {
    int32_t x = 0;
    int32_t y = 0;
};

inline TracePoint unpackTracePoint(int32_t packed)
{
    TracePoint p;
    p.x = (packed >> 16) & 0xFFFF;
    p.y = packed & 0xFFFF;
    return p;
}

// The key code a tap carries is a Qt::Key, which for the letters and digits the
// keyboard sends equals the character; everything else is not a letter we can
// spell with. We take only the printable ASCII range and lowercase it, because
// hunspell is asked about the word, and case is decided by the keyboard's own
// shift state when it shows the candidate.
inline bool keyToChar(int32_t key, char& out)
{
    if (key >= 'A' && key <= 'Z') { out = static_cast<char>(key - 'A' + 'a'); return true; }
    if (key >= 'a' && key <= 'z') { out = static_cast<char>(key);             return true; }
    if (key >= '0' && key <= '9') { out = static_cast<char>(key);             return true; }
    return false;
}

// The word a sequence of taps spells, dropping anything that is not a letter or
// digit (a space, a backspace, punctuation): the keyboard only asks for a
// suggestion while a word is being typed, and sends the taps of that word.
inline std::string wordFromTaps(const std::vector<Tap>& taps)
{
    std::string word;
    word.reserve(taps.size());
    for (const Tap& tap : taps) {
        char c;
        if (keyToChar(tap.key, c))
            word += c;
    }
    return word;
}

// --- processTaps: building the reply -----------------------------------------

// The guesses for a typed word, in the order the keyboard expects: the word as
// typed first, then up to maxCorrections corrections from the engine. A
// misspelling is marked for autocorrection on its top correction, so the
// composing line shows the fix; a correctly spelled word is left alone.
inline std::vector<Guess> guessesFor(const Engine& engine, const std::string& typed,
                                     size_t maxCorrections = 5)
{
    std::vector<Guess> guesses;
    if (typed.empty())
        return guesses;

    const bool typedOk = engine.spelled(typed);

    Guess first;
    first.str = typed;
    first.spelled = typedOk;
    first.autoAccept = false; // the typed word is never the autocorrection
    guesses.push_back(first);

    for (const std::string& suggestion : engine.suggest(typed)) {
        if (guesses.size() >= 1 + maxCorrections)
            break;
        if (suggestion == typed)
            continue; // never list the typed word twice
        Guess g;
        g.str = suggestion;
        g.spelled = true; // a suggestion is by definition a dictionary word
        // Autocorrect to the first correction, and only when the typed word is
        // not itself a word: correcting a correctly spelled word is what makes
        // autocorrect infuriating.
        g.autoAccept = !typedOk && guesses.size() == 1;
        guesses.push_back(g);
    }
    return guesses;
}

// Serialise one guess as the keyboard reads it. "auto-accept" carries a literal
// hyphen; it is only emitted when true, as HP's own replies did, so a reader
// cannot mistake a missing flag for false.
inline std::string guessToJson(const Guess& g)
{
    std::string out = "{\"str\":\"";
    out += jsonEscape(g.str);
    out += "\",\"sp\":";
    out += g.spelled ? "true" : "false";
    if (g.autoAccept)
        out += ",\"auto-accept\":true";
    out += "}";
    return out;
}

// The whole processTaps reply. "traceEntry" says whether this answers a swipe
// rather than taps, and MUST be present or the keyboard throws the reply away.
// "spelledCorrectly" is true when the typed word is a dictionary word; the
// keyboard uses it to decide between guesses[0] and guesses[1] when nothing is
// auto-accepted.
inline std::string processTapsReply(bool traceEntry, bool spelledCorrectly,
                                    const std::vector<Guess>& guesses)
{
    std::string out = "{\"traceEntry\":";
    out += traceEntry ? "true" : "false";
    out += ",\"spelledCorrectly\":";
    out += spelledCorrectly ? "true" : "false";
    out += ",\"returnValue\":true,\"guesses\":[";
    for (size_t i = 0; i < guesses.size(); ++i) {
        if (i)
            out += ",";
        out += guessToJson(guesses[i]);
    }
    out += "]}";
    return out;
}

// The empty reply: a well-formed answer that clears the candidate bar. Still
// carries traceEntry so the keyboard does not discard it.
inline std::string emptyTapsReply(bool traceEntry)
{
    return processTapsReply(traceEntry, true, {});
}

// Decode taps and answer. The one entry point for the keyboard's normal typing.
inline std::string answerTaps(const Engine& engine, const std::vector<Tap>& taps)
{
    const std::string word = wordFromTaps(taps);
    if (word.empty())
        return emptyTapsReply(false);
    const bool ok = engine.spelled(word);
    return processTapsReply(false, ok, guessesFor(engine, word));
}

// Decode a trace and answer. A swipe cannot be spelled out letter by letter the
// way taps can -- the path crosses many keys -- so the honest answer today is
// the first/last letters the keyboard already resolved, offered as a prefix to
// complete. This keeps traceEntry:true so the keyboard knows it was a swipe.
inline std::string answerTrace(const Engine& engine, const std::string& first,
                               const std::string& last)
{
    // first/last are strings of candidate characters under the endpoints; take
    // the first character of each as the word's bounds. With only two letters
    // there is nothing to spell-check, so we offer the engine's completions of
    // the leading letter and mark none auto-accepted: a swipe should never
    // silently replace what the user meant.
    std::string stub;
    if (!first.empty())
        stub += first.front();
    if (!last.empty())
        stub += last.front();
    if (stub.empty())
        return emptyTapsReply(true);

    std::vector<Guess> guesses;
    Guess typed;
    typed.str = stub;
    typed.spelled = engine.spelled(stub);
    guesses.push_back(typed);
    for (const std::string& s : engine.suggest(stub)) {
        if (guesses.size() >= 6)
            break;
        if (s == stub)
            continue;
        Guess g;
        g.str = s;
        g.spelled = true;
        guesses.push_back(g);
    }
    return processTapsReply(true, typed.spelled, guesses);
}

// --- search: the browser's single best completion ----------------------------

// {"query": s} -> {"returnValue": bool, "match": s}. returnValue is true only
// when there is a match to show; the browser treats a false or a missing match
// as "no completion" (reference/luna-sysmgr-ce/Src/webbase/WebPage.cpp:1095).
// A correctly spelled query matches itself; a misspelling matches its top
// correction; a word with neither matches nothing.
inline std::string searchReply(const Engine& engine, const std::string& query)
{
    if (query.empty())
        return "{\"returnValue\":false}";
    if (engine.spelled(query))
        return std::string("{\"returnValue\":true,\"match\":\"") + jsonEscape(query) + "\"}";
    const std::vector<std::string> suggestions = engine.suggest(query);
    if (suggestions.empty())
        return "{\"returnValue\":false}";
    return std::string("{\"returnValue\":true,\"match\":\"") + jsonEscape(suggestions.front()) + "\"}";
}

// --- learn: add a word to the user dictionary --------------------------------

// {"word": s} -> {"returnValue": bool}. The browser calls this fire-and-forget
// and never reads the reply, but a well-behaved service answers anyway. An
// empty word is a bad request, not a learned word.
inline std::string learnReply(Engine& engine, const std::string& word)
{
    if (word.empty())
        return "{\"returnValue\":false,\"errorText\":\"expected {\\\"word\\\": non-empty string}\"}";
    const bool ok = engine.learn(word);
    if (ok)
        return "{\"returnValue\":true}";
    return "{\"returnValue\":false,\"errorText\":\"could not add the word to the user dictionary\"}";
}

} // namespace SmartKey

#endif // SMARTKEY_CONTRACT_H
