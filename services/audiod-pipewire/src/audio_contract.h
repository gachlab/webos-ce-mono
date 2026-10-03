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

#ifndef AUDIOD_PIPEWIRE_AUDIO_CONTRACT_H
#define AUDIOD_PIPEWIRE_AUDIO_CONTRACT_H

//
// The com.palm.audio bus contract, as its surviving consumers actually read it,
// kept bus-free and header-only so the payloads can be checked as strings
// without an ls-hubd. com.palm.audio is HP's audiod; nothing in the CE drop
// provides it, so every control in the shell that depends on it is inert. This
// header is the half of the service that does not touch PipeWire: what to say on
// the bus, and when.
//
// MEASURED against the two consumers that are still in the tree, because the
// ticket's own summary is looser than the code:
//
//   NativeAlertManager (the volume HUD). Subscribes to com.palm.audio/{system,
//   media,ringtone,phone}/status when the service appears. cbAudioControlsChanged
//   pops the HUD only when the status payload carries, together:
//       "action"        a string that is not "requested"
//       "scenario"      one fixed name, matched with ==: system_default,
//                       ringtone_default, media_back_speaker, media_a2dp,
//                       media_headset, media_headset_mic, or a phone_* name
//       "volume"        int
//       "changed"       an array that contains "volume" (and NOT "scenario":
//                       a scenario change in the same message suppresses the HUD)
//       "ringer switch" bool (note the space; that is the field name audiod used)
//
//   DisplayManager. Subscribes to com.palm.audio/phone/status only, and
//   VALIDATE_SCHEMA requires all three of {"action":string, "scenario":string,
//   "active":boolean}. It wakes the screen when scenario begins "phone_headset"
//   and action is "enabled"/"disabled". Not exercised on a desktop with no
//   phone, but the schema is required, so phone/status must always carry those
//   three or DisplayManager drops the message.
//
// Schema validation is global-off (luna.conf schemaValidationOption=0), so extra
// fields are tolerated; the required ones still have to be present. We therefore
// send the superset: every field either consumer reads, every time.
//

#include <string>

namespace AudioContract {

// The four categories HP's audiod published and the shell subscribes to, as the
// path segment in com.palm.audio/<category>/status.
enum class Category { System, Media, Ringtone, Phone };

inline const char* categoryPath(Category c)
{
    switch (c) {
    case Category::System:   return "system";
    case Category::Media:    return "media";
    case Category::Ringtone: return "ringtone";
    case Category::Phone:    return "phone";
    }
    return "system";
}

// The default scenario name for each category, matched with == on the consumer
// side, so these strings are not free text. The media default is back_speaker:
// the laptop's own speakers, the honest reading when no headset or A2DP sink is
// the route. The others have exactly one default name in the consumer.
inline const char* defaultScenario(Category c)
{
    switch (c) {
    case Category::System:   return "system_default";
    case Category::Media:    return "media_back_speaker";
    case Category::Ringtone: return "ringtone_default";
    case Category::Phone:    return "phone_back_speaker";
    }
    return "system_default";
}

// A JSON string literal escaped for embedding. The scenario and action strings
// here are all our own fixed vocabulary, never user input, but routing a device
// name into a payload later (the output chooser) will need escaping, so it lives
// here from the start.
inline std::string jsonEscape(const std::string& in)
{
    std::string out;
    out.reserve(in.size() + 2);
    for (char ch : in) {
        switch (ch) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += ch;     break;
        }
    }
    return out;
}

// webOS's volume is a 0..100 int on the bus. PipeWire/PulseAudio volume is a
// float where 1.0 is 100%. These two conversions are the only place the two
// scales meet, so the rounding is defined once.
inline int volumeToPercent(double linear)
{
    if (linear < 0.0)
        linear = 0.0;
    if (linear > 1.0)
        linear = 1.0;
    return static_cast<int>(linear * 100.0 + 0.5);
}

inline double percentToVolume(int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    return static_cast<double>(percent) / 100.0;
}

// The status payload both consumers read. withChangedVolume=true marks it as a
// volume change so NativeAlertManager pops the HUD; false is a plain state
// announcement (e.g. a scenario appearing) that must NOT pop it. The "changed"
// array is the switch: ["volume"] pops the HUD, ["scenario"] suppresses it, and
// the empty array is a quiet state carry.
//
// action is never "requested" here (that is the one value NativeAlertManager
// ignores); "changed" is the honest description of what moved.
inline std::string statusPayload(const char* scenario,
                                 int volumePercent,
                                 bool muted,
                                 bool withChangedVolume,
                                 bool withReturnValue)
{
    std::string p = "{";
    if (withReturnValue)
        p += "\"returnValue\":true,";
    p += "\"action\":\"changed\",";
    p += std::string("\"scenario\":\"") + scenario + "\",";
    p += "\"volume\":" + std::to_string(volumePercent) + ",";
    // active is what DisplayManager's schema requires on phone/status; it means
    // "this scenario is the one in effect", which for a single-output desktop is
    // always true for the category being announced.
    p += "\"active\":true,";
    p += std::string("\"muted\":") + (muted ? "true" : "false") + ",";
    // ringer switch: the physical mute slider on a Pre. There is none here; it
    // reads not-silenced (true) so NativeAlertManager draws the volume HUD and
    // not the "ringer off" one. The space in the name is audiod's, not a typo.
    p += std::string("\"ringer switch\":") + (muted ? "false" : "true") + ",";
    p += std::string("\"changed\":[") + (withChangedVolume ? "\"volume\"" : "") + "]";
    p += "}";
    return p;
}

// What a volume key press maps to. The shell publishes only {"key":..,
// "state":"up"} -- the press half never arrives -- so a tap is one step. The
// step is 100/16 rounded, matching the 16 ticks the Pre's volume had, which is
// also what the HUD artwork is cut into.
inline int volumeStep()
{
    return 100 / 16;
}

inline int applyVolumeKey(int currentPercent, const std::string& key)
{
    int next = currentPercent;
    if (key == "volume_up")
        next += volumeStep();
    else if (key == "volume_down")
        next -= volumeStep();
    if (next < 0)
        next = 0;
    if (next > 100)
        next = 100;
    return next;
}

// The output chooser reads a list of outputs: each is {id, name, current}. The
// chooser lists them by their real names and marks the one webOS is on. The id
// is opaque to the UI -- it goes straight back in a selectOutput call. Built
// here so the shape is checked as a string without a bus or a sound server.
//
// One output on the payload. The caller concatenates these between the array
// brackets with commas; keeping the single-entry form here keeps the escaping
// (names carry spaces and, on some hosts, quotes) in one place.
inline std::string outputEntry(const std::string& id, const std::string& name, bool current)
{
    std::string e = "{";
    e += "\"id\":\"" + jsonEscape(id) + "\",";
    e += "\"name\":\"" + jsonEscape(name) + "\",";
    e += std::string("\"current\":") + (current ? "true" : "false");
    e += "}";
    return e;
}

} // namespace AudioContract

#endif // AUDIOD_PIPEWIRE_AUDIO_CONTRACT_H
