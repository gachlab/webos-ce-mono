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

#include "hunspell_engine.h"

#include <hunspell/hunspell.hxx>

#include <fstream>
#include <sys/stat.h>

namespace SmartKey {

namespace {

bool fileExists(const std::string& path)
{
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// Create every directory in the path up to the last '/', the way `mkdir -p`
// does. Used before writing the user word list, whose parent
// (/var/luna/preferences) may not exist yet on a clean boot. Best effort: a
// failure is reported by the open that follows.
void makeParentDirs(const std::string& path)
{
    std::string::size_type slash = path.find('/', 1);
    while (slash != std::string::npos) {
        const std::string dir = path.substr(0, slash);
        if (!dir.empty())
            ::mkdir(dir.c_str(), 0755); // errors (incl. EEXIST) ignored on purpose
        slash = path.find('/', slash + 1);
    }
}

bool dictPairExists(const std::string& dir, const std::string& stem)
{
    return fileExists(dir + "/" + stem + ".dic") && fileExists(dir + "/" + stem + ".aff");
}

} // namespace

// "es-MX.UTF-8" -> "es_MX"; "es" -> "es"; "en_US" -> "en_US". Cuts at the first
// '.' or '@' (encoding/modifier), turns '-' into '_', and leaves the rest.
std::string HunspellEngine::localeToStem(const std::string& locale)
{
    std::string stem;
    for (char c : locale) {
        if (c == '.' || c == '@')
            break;
        stem += (c == '-') ? '_' : c;
    }
    return stem;
}

std::string HunspellEngine::resolveDictStem(const std::string& locale, const std::string& dictDir)
{
    const std::string stem = localeToStem(locale);
    // Exact match: es_MX, en_US.
    if (!stem.empty() && dictPairExists(dictDir, stem))
        return stem;
    // Language only: "es_MX" with no es_MX files -> try es_ES then es.
    const std::string lang = stem.substr(0, stem.find('_'));
    if (!lang.empty()) {
        // The common packaged stem is lang_LANGUPPER for es (es_ES); try it.
        std::string upper = lang;
        for (char& c : upper)
            c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));
        if (dictPairExists(dictDir, lang + "_" + upper))
            return lang + "_" + upper;
        if (dictPairExists(dictDir, lang))
            return lang;
    }
    // The floor: en_US, always installed. If even that is gone, give up.
    if (dictPairExists(dictDir, "en_US"))
        return "en_US";
    return std::string();
}

HunspellEngine::HunspellEngine(const std::string& locale,
                               const std::string& dictDir,
                               const std::string& userWordListPath)
    : m_userWordListPath(userWordListPath)
{
    const std::string stem = resolveDictStem(locale, dictDir);
    if (stem.empty())
        return; // no dictionary at all; ready() stays false

    const std::string aff = dictDir + "/" + stem + ".aff";
    const std::string dic = dictDir + "/" + stem + ".dic";
    m_hunspell = std::make_unique<Hunspell>(aff.c_str(), dic.c_str());
    m_activeLocale = stem;

    // The user's own words, taught through learn, each added with hunspell's
    // add() so they count as correctly spelled. One word per line, '#' comments
    // skipped; this is the format learn() appends to.
    if (!m_userWordListPath.empty() && fileExists(m_userWordListPath)) {
        std::ifstream in(m_userWordListPath);
        std::string word;
        while (std::getline(in, word)) {
            if (!word.empty() && word[0] != '#')
                m_hunspell->add(word);
        }
    }
}

HunspellEngine::~HunspellEngine() = default;

bool HunspellEngine::spelled(const std::string& word) const
{
    if (!m_hunspell || word.empty())
        return false;
    return m_hunspell->spell(word);
}

std::vector<std::string> HunspellEngine::suggest(const std::string& word) const
{
    if (!m_hunspell || word.empty())
        return {};
    return m_hunspell->suggest(word);
}

bool HunspellEngine::learn(const std::string& word)
{
    if (!m_hunspell || word.empty())
        return false;
    // In-memory first, so it takes effect for this session immediately. A
    // non-zero return from hunspell's add means it could not take the word.
    if (m_hunspell->add(word) != 0)
        return false;
    // Then persisted, so it survives a restart. Appending one word per line is
    // the format the loader above reads back. The parent directory
    // (/var/luna/preferences) may not exist on a clean boot, so it is created
    // first; a missing file is created by the append.
    if (!m_userWordListPath.empty()) {
        makeParentDirs(m_userWordListPath);
        std::ofstream out(m_userWordListPath, std::ios::app);
        if (!out)
            return false;
        out << word << '\n';
        return static_cast<bool>(out);
    }
    return true;
}

} // namespace SmartKey
