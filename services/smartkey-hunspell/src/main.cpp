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

//
// com.palm.smartKey, answered from hunspell.
//
// HP's own smartKey -- the spelling and word-completion service behind the
// keyboard's candidate bar and the browser's text fields -- was never released
// as source. This is ours, answering the same bus name from libhunspell, the
// open-source checker HP itself used, with the dictionaries taken from the host
// (/usr/share/hunspell) rather than vendored. Nothing proprietary ships.
//
// Three methods, the only ones any client in the tree calls:
//
//   processTaps   the on-screen keyboard (keyboard-efigs/CandidateBarRemote)
//   search        the browser's text fields (BrowserServer/BrowserPage)
//   learn         add a word to the user dictionary; fire-and-forget
//
// The shaping of every reply -- and the decoding of the keyboard's packed taps
// and trace -- is in smartkey_contract.h, free of the bus and of hunspell, so
// tests/smartkey-contract.cpp pins it without ls-hubd and without a dictionary.
// The dictionary itself is behind SmartKey::Engine, implemented here by
// HunspellEngine. This file is the registration, the JSON parsing of each
// request, and the method table.
//
// Registered on both buses because its callers are on both: the keyboard is in
// the shell (private) and the browser is an app (public).
//

#include "smartkey_contract.h"
#include "hunspell_engine.h"

#include <luna-service2/lunaservice.h>

#include <cjson/json.h>
#include <glib.h>
#include <glib-unix.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {

const char kServiceName[] = "com.palm.smartKey";
const char kCategory[] = "/";

// Where the host keeps hunspell's dictionaries. Overridable for a test or an
// unusual install through SMARTKEY_DICT_DIR.
const char kDefaultDictDir[] = "/usr/share/hunspell";
// The user's taught words, beside the system preferences so they survive a
// restart the way the rest of the session's state does.
const char kUserWordList[] = "/var/luna/preferences/com.palm.smartKey.userwords";

GMainLoop* g_loop = nullptr;
LSPalmService* g_service = nullptr;
std::unique_ptr<SmartKey::Engine> g_engine;

void logAndFree(const char* where, LSError& error)
{
    g_warning("smartkey-hunspell: %s: %s", where, error.message ? error.message : "(no message)");
    LSErrorFree(&error);
}

void reply(LSHandle* sh, LSMessage* message, const std::string& payload)
{
    LSError error;
    LSErrorInit(&error);
    if (!LSMessageReply(sh, message, payload.c_str(), &error))
        logAndFree("LSMessageReply", error);
}

// The request's parsed JSON object, or nullptr; the caller frees it with
// json_object_put.
json_object* requestOf(LSMessage* message)
{
    const char* payload = LSMessageGetPayload(message);
    if (!payload)
        return nullptr;
    json_object* root = json_tokener_parse(payload);
    if (!root || is_error(root))
        return nullptr;
    if (!json_object_is_type(root, json_type_object)) {
        json_object_put(root);
        return nullptr;
    }
    return root;
}

std::string stringMember(json_object* object, const char* name)
{
    if (!object)
        return std::string();
    json_object* value = json_object_object_get(object, name);
    if (value && !is_error(value) && json_object_is_type(value, json_type_string))
        return json_object_get_string(value);
    return std::string();
}

bool hasArrayMember(json_object* object, const char* name, json_object*& out)
{
    out = object ? json_object_object_get(object, name) : nullptr;
    return out && !is_error(out) && json_object_is_type(out, json_type_array);
}

// --- the bus methods ---------------------------------------------------------

// The keyboard: taps or a trace, answered with suggestions and an
// autocorrection. See smartkey_contract.h for the request's two shapes and the
// reply the candidate bar reads.
bool processTaps(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = requestOf(message);
    if (!root) {
        // A malformed request still gets a well-formed empty reply, so the
        // keyboard clears its bar rather than hanging on a dropped call.
        reply(sh, message, SmartKey::emptyTapsReply(false));
        return true;
    }

    json_object* array = nullptr;
    if (hasArrayMember(root, "taps", array)) {
        std::vector<SmartKey::Tap> taps;
        const int n = json_object_array_length(array);
        // Four entries per tap: x, y, key, shift. A trailing partial group is
        // ignored rather than guessed at.
        for (int i = 0; i + 3 < n; i += 4) {
            SmartKey::Tap tap;
            tap.x = json_object_get_int(json_object_array_get_idx(array, i));
            tap.y = json_object_get_int(json_object_array_get_idx(array, i + 1));
            tap.key = json_object_get_int(json_object_array_get_idx(array, i + 2));
            tap.shift = json_object_get_boolean(json_object_array_get_idx(array, i + 3));
            taps.push_back(tap);
        }
        reply(sh, message, SmartKey::answerTaps(*g_engine, taps));
    } else if (hasArrayMember(root, "trace", array)) {
        // The packed trace[] (((x&0xFFFF)<<16)|y per point) is not decoded here
        // on purpose: a swipe crosses many keys and cannot be spelled out letter
        // by letter, so answerTrace works from the endpoints the keyboard
        // already resolved (first/last). unpackTracePoint exists for when a
        // real gesture recogniser is wired in. See answerTrace in the contract.
        const std::string first = stringMember(root, "first");
        const std::string last = stringMember(root, "last");
        reply(sh, message, SmartKey::answerTrace(*g_engine, first, last));
    } else {
        reply(sh, message, SmartKey::emptyTapsReply(false));
    }

    json_object_put(root);
    return true;
}

// The browser: a single best completion for a query.
bool search(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = requestOf(message);
    const std::string query = stringMember(root, "query");
    if (root)
        json_object_put(root);
    reply(sh, message, SmartKey::searchReply(*g_engine, query));
    return true;
}

// The browser: add a word to the user dictionary. Fire-and-forget on the
// caller's side, but answered anyway.
bool learn(LSHandle* sh, LSMessage* message, void*)
{
    json_object* root = requestOf(message);
    const std::string word = stringMember(root, "word");
    if (root)
        json_object_put(root);
    reply(sh, message, SmartKey::learnReply(*g_engine, word));
    return true;
}

LSMethod kMethods[] = {
    { "processTaps", processTaps },
    { "search", search },
    { "learn", learn },
    { },
};

gboolean quit(gpointer)
{
    g_main_loop_quit(g_loop);
    return G_SOURCE_REMOVE;
}

std::string envOr(const char* name, const char* fallback)
{
    const char* value = ::getenv(name);
    return (value && *value) ? value : fallback;
}

} // namespace

int main()
{
    g_loop = g_main_loop_new(nullptr, FALSE);

    // The locale follows the host's LANG, normalised to a dictionary stem, with
    // en_US as the floor an unknown locale falls to (#19). SMARTKEY_LOCALE wins
    // when set, which is how the Language setting will drive it live later.
    const std::string locale = envOr("SMARTKEY_LOCALE", envOr("LANG", "en_US").c_str());
    const std::string dictDir = envOr("SMARTKEY_DICT_DIR", kDefaultDictDir);
    const std::string userWords = envOr("SMARTKEY_USER_WORDS", kUserWordList);

    auto engine = std::make_unique<SmartKey::HunspellEngine>(locale, dictDir, userWords);
    if (!engine->ready()) {
        g_warning("smartkey-hunspell: no hunspell dictionary found under %s "
                  "(wanted locale '%s', and en_US was absent too); "
                  "answering as an empty dictionary", dictDir.c_str(), locale.c_str());
    } else {
        g_message("smartkey-hunspell: dictionary '%s' loaded for locale '%s'",
                  engine->activeLocale().c_str(), locale.c_str());
    }
    g_engine = std::move(engine);

    LSError error;
    LSErrorInit(&error);
    if (!LSRegisterPalmService(kServiceName, &g_service, &error)) {
        logAndFree("LSRegisterPalmService", error);
        return 1;
    }

    LSErrorInit(&error);
    if (!LSPalmServiceRegisterCategory(g_service, kCategory, kMethods, kMethods,
                                       nullptr, nullptr, &error)) {
        logAndFree("LSPalmServiceRegisterCategory", error);
        return 1;
    }

    LSErrorInit(&error);
    if (!LSGmainAttachPalmService(g_service, g_loop, &error)) {
        logAndFree("LSGmainAttachPalmService", error);
        return 1;
    }

    g_unix_signal_add(SIGTERM, quit, nullptr);
    g_unix_signal_add(SIGINT, quit, nullptr);

    g_message("smartkey-hunspell: com.palm.smartKey up");
    g_main_loop_run(g_loop);

    LSErrorInit(&error);
    if (!LSUnregisterPalmService(g_service, &error))
        logAndFree("LSUnregisterPalmService", error);
    g_engine.reset();
    g_main_loop_unref(g_loop);
    return 0;
}
