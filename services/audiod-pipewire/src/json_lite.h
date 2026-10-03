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

#ifndef AUDIOD_PIPEWIRE_JSON_LITE_H
#define AUDIOD_PIPEWIRE_JSON_LITE_H

//
// A thin, modern reading layer over the JSON library the Luna bus already uses.
//
// This is NOT an adapter in the repo's sense: an adapter maps an interface HP's
// code already calls onto a modern library, so HP's sources compile unchanged.
// Nothing of HP's calls this -- it is our own new service's code. So rather than
// pull a second JSON library into a process whose luna-service2 is already built
// against the bus's json-c (HP's cjson, API json_object_*), we keep that one
// library and give our own code a small RAII reading view of it:
//
//   * the parsed root frees itself (no hand-written json_object_put on every
//     early return, which is where the bus's C callers leak);
//   * reads return std::optional, so a missing or wrong-typed field is a value
//     to test, not a null pointer to remember to check.
//
// Reading only, on purpose: the status payloads this service sends are built as
// plain strings in audio_contract.h, where their exact shape is the contract and
// is read as text in tests. This header is for the three inbound payloads
// (volume key, preferences, playFeedback), which only need to be inspected.
//

#include <cjson/json.h>

#include <optional>
#include <string>

namespace JsonLite {

// Owns a parsed json_object tree and releases it in the destructor. Move-only.
class Document {
public:
    explicit Document(const char* text)
        : m_root(text ? json_tokener_parse(text) : nullptr)
    {
        if (m_root && is_error(m_root))
            m_root = nullptr;
    }

    ~Document()
    {
        if (m_root)
            json_object_put(m_root);
    }

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&& o) noexcept : m_root(o.m_root) { o.m_root = nullptr; }
    Document& operator=(Document&& o) noexcept
    {
        if (this != &o) {
            if (m_root)
                json_object_put(m_root);
            m_root = o.m_root;
            o.m_root = nullptr;
        }
        return *this;
    }

    bool valid() const { return m_root != nullptr; }

    // Borrowed; the Document stays the owner.
    json_object* root() const { return m_root; }

private:
    json_object* m_root = nullptr;
};

// Field reads. Each returns nullopt when the key is absent or not of the asked
// type, so the caller never dereferences a raw json_object.
inline std::optional<std::string> getString(json_object* obj, const char* key)
{
    if (!obj)
        return std::nullopt;
    json_object* field = json_object_object_get(obj, key);
    if (!field)
        return std::nullopt;
    const char* s = json_object_get_string(field);
    if (!s)
        return std::nullopt;
    return std::string(s);
}

inline std::optional<bool> getBool(json_object* obj, const char* key)
{
    if (!obj)
        return std::nullopt;
    json_object* field = json_object_object_get(obj, key);
    if (!field || !json_object_is_type(field, json_type_boolean))
        return std::nullopt;
    return json_object_get_boolean(field) != 0;
}

inline std::optional<int> getInt(json_object* obj, const char* key)
{
    if (!obj)
        return std::nullopt;
    json_object* field = json_object_object_get(obj, key);
    if (!field || !json_object_is_type(field, json_type_int))
        return std::nullopt;
    return json_object_get_int(field);
}

} // namespace JsonLite

#endif // AUDIOD_PIPEWIRE_JSON_LITE_H
