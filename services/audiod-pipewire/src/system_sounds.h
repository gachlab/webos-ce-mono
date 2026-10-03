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

#ifndef AUDIOD_PIPEWIRE_SYSTEM_SOUNDS_H
#define AUDIOD_PIPEWIRE_SYSTEM_SOUNDS_H

//
// systemsounds/playFeedback: the shell's SoundPlayerPool calls this with a
// {"name": string} and expects audiod to play the matching file. Bus-free and
// header-only so the name->file mapping can be tested without a sound server.
//
// The mapping is the honest half: 12 files ship in /usr/palm/sounds, and the
// names SoundPlayerPool passes ("key", "space", "carddrag", ...) do not all have
// one. A name with a file plays it; a name without one plays nothing, rather
// than inventing a sound HP never shipped. The audible UI-click set was never in
// the CE drop; forcing one would be a product decision, not a port.
//
// Resolution is name -> "<soundsDir>/<file>", chosen so the file set can grow
// without code change: a new name whose file is dropped into /usr/palm/sounds
// starts playing. Playback itself is the service's job (spawn a PipeWire player
// so the sound is webOS's own stream, under webOS's volume); this header only
// decides *which* file, or none.
//

#include <string>

namespace SystemSounds {

// The named feedback events SoundPlayerPool emits, mapped to the files that
// actually ship. Only mappings whose file exists in the CE drop are listed; an
// unlisted name resolves to empty (play nothing). Kept as a flat switch rather
// than a map so it is a pure function with no static init.
inline std::string fileForName(const std::string& name)
{
    // Shipping files (components/luna-sysmgr/sounds -> /usr/palm/sounds):
    //   alert.wav notification.wav phone.wav ringtone.mp3 boot.mp3 shutdown.mp3
    //   error.mp3 panel.mp3 charging.mp3 battery_full.mp3 battery_low.mp3
    //   tap_to_share.mp3
    if (name == "alert")        return "alert.wav";
    if (name == "notification") return "notification.wav";
    if (name == "ringtone")     return "ringtone.mp3";
    if (name == "boot")         return "boot.mp3";
    if (name == "shutdown")     return "shutdown.mp3";
    if (name == "error")        return "error.mp3";
    if (name == "panel")        return "panel.mp3";
    if (name == "charging")     return "charging.mp3";
    if (name == "battery_full") return "battery_full.mp3";
    if (name == "battery_low")  return "battery_low.mp3";
    if (name == "tap_to_share") return "tap_to_share.mp3";
    // "phone" is the ringtone/alert file, not the UI phone sound.
    if (name == "phone")        return "phone.wav";
    // key, space, return, backspace, carddrag and the rest: no file shipped.
    return std::string();
}

// The absolute path to play, or empty if the name has no file. soundsDir has no
// trailing slash.
inline std::string pathForName(const std::string& soundsDir, const std::string& name)
{
    const std::string file = fileForName(name);
    if (file.empty())
        return std::string();
    return soundsDir + "/" + file;
}

} // namespace SystemSounds

#endif // AUDIOD_PIPEWIRE_SYSTEM_SOUNDS_H
