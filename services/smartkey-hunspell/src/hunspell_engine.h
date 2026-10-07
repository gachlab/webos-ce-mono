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

#ifndef SMARTKEY_HUNSPELL_ENGINE_H
#define SMARTKEY_HUNSPELL_ENGINE_H

//
// The SmartKey::Engine implemented on libhunspell -- the open-source spell
// checker and morphological analyser HP itself used, taken from the
// distribution rather than vendored (the .dic/.aff live in /usr/share/hunspell
// on the host, like QtWebEngine's library). Nothing proprietary ships: the
// service reaches for whatever dictionaries the host has installed.
//
// Dictionaries are chosen by locale with en_US as the floor, which is always
// present (hunspell-en-us) and is the fallback an unknown locale falls to --
// the same rule the date/time pickers follow (#19, PR #90): an unknown locale
// shows en_US, not a broken feature. es_ES is used when hunspell-es is
// installed, and its regional aliases (es_MX, es_AR, ...) all resolve to it.
//
// A per-user word list sits beside the system preferences
// (/var/luna/preferences) and is read in word by word at startup and appended
// to on learn, so a word the user taught survives a restart.
//
// This uses only hunspell 1.7's current API -- spell(const std::string&),
// suggest(const std::string&), add(const std::string&) -- and none of the
// H_DEPRECATED char** overloads.
//

#include "smartkey_contract.h"

#include <memory>
#include <string>
#include <vector>

class Hunspell;

namespace SmartKey {

class HunspellEngine : public Engine {
public:
    // Build for a locale (e.g. "es_ES", "en_US", or a host locale like
    // "es-MX"/"es_MX.UTF-8" which is normalised). dictDir is where the .dic/.aff
    // live; userWordListPath is the per-user list, loaded if present and
    // appended to on learn. If the locale's dictionary is missing, en_US is used
    // instead, and ready() still returns true. If even en_US is missing,
    // ready() returns false and the methods answer as an empty dictionary.
    HunspellEngine(const std::string& locale,
                   const std::string& dictDir,
                   const std::string& userWordListPath);
    ~HunspellEngine() override;

    HunspellEngine(const HunspellEngine&) = delete;
    HunspellEngine& operator=(const HunspellEngine&) = delete;

    bool ready() const { return m_hunspell != nullptr; }
    // The locale whose dictionary actually loaded (may differ from the one
    // asked for, when it fell back to en_US).
    const std::string& activeLocale() const { return m_activeLocale; }

    bool spelled(const std::string& word) const override;
    std::vector<std::string> suggest(const std::string& word) const override;
    bool learn(const std::string& word) override;

    // The .dic/.aff pair that exists for a locale under dictDir, resolving a
    // host locale ("es-MX.UTF-8") to a file stem ("es_MX") and falling back to
    // en_US. Static and free of hunspell so a test can check the resolution
    // without a dictionary loaded. Returns the stem ("es_ES") or "" if neither
    // the locale nor en_US is present.
    static std::string resolveDictStem(const std::string& locale, const std::string& dictDir);
    // The file stem for a locale string, with no filesystem check: "es-MX.UTF-8"
    // -> "es_MX". Lets the resolution be tested in parts.
    static std::string localeToStem(const std::string& locale);

private:
    std::unique_ptr<Hunspell> m_hunspell;
    std::string m_activeLocale;
    std::string m_userWordListPath;
};

} // namespace SmartKey

#endif // SMARTKEY_HUNSPELL_ENGINE_H
