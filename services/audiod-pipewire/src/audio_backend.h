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

#ifndef AUDIOD_PIPEWIRE_AUDIO_BACKEND_H
#define AUDIOD_PIPEWIRE_AUDIO_BACKEND_H

//
// The audio engine behind com.palm.audio, as an interface so the bus half
// (main.cpp) never names a sound library. There is one implementation,
// PulseBackend, and the choice of library is deliberate and measured, not
// incidental -- see pulse_backend.h.
//
// The division of this interface is the ticket's own, drawn along the line the
// measurement found:
//
//   * Volume and mute are PROVEN. The session already appears as its own
//     PipeWire node (application.name="WebAppManager", client.api=
//     "pipewire-pulse"), with a volume of its own that QtWebEngine created.
//     set_sink_input_volume on it is "webOS's volume"; it needs nothing
//     invented, and nothing from the session manager.
//
//   * Moving webOS's audio to a chosen output is NOT settled, and this is the
//     PipeWire-vs-WirePlumber question in full. Two attempts on the running
//     session failed: writing target.object as a node name did nothing, and as
//     an object.serial it was reverted within three seconds -- plausibly by a
//     custom WirePlumber Lua component that rewrites preferred devices. Until
//     the measurement in moveOutputToTarget()'s comment separates "the write
//     never lands" (a PipeWire/command problem) from "the session manager
//     reverts it" (a WirePlumber/policy problem), the move is left unimplemented
//     behind this interface. The volume half, the HUD and the chooser's UI do
//     not depend on it.
//

#include <functional>
#include <string>
#include <vector>

namespace Audio {

// One selectable output, as the chooser lists it. id is whatever the backend
// needs to move audio there (for PulseAudio, the sink index as a string);
// name is the human label shown in the drawer.
struct Output {
    std::string id;
    std::string name;
    bool isDefault = false;
};

// The engine's current view of webOS's own stream, pushed to the bus half
// whenever it changes so the HUD and the slider stay in step with changes made
// outside webOS (the host's own mixer moving our node).
struct StreamState {
    bool present = false;   // whether the session's sink-input exists yet
    int volumePercent = 100;
    bool muted = false;
};

class Backend {
public:
    virtual ~Backend() = default;

    // Called once the glib main loop is running. Connects to a PipeWire if one
    // is up; the phone case (bring one up when there is no session) is a later
    // change and is why this can fail without the service exiting -- it retries.
    virtual bool start() = 0;

    // webOS's volume, applied to the session's own stream. 0..100.
    virtual void setVolumePercent(int percent) = 0;
    virtual void setMuted(bool muted) = 0;

    // The last known state of the session's stream.
    virtual StreamState state() const = 0;

    // The outputs the chooser can offer. Populated for the UI; moving to one is
    // the unsettled half below.
    virtual std::vector<Output> outputs() const = 0;

    // Send webOS's audio to a chosen output.
    //
    // UNIMPLEMENTED on purpose: this is exactly the PipeWire-vs-WirePlumber
    // decision the measurement has to make first. See audio_backend.h's header
    // and pulse_backend.cpp. Returns false until a measured path is chosen.
    virtual bool moveOutputToTarget(const std::string& outputId) = 0;

    // The backend calls this whenever the stream's state changes underneath us,
    // so main.cpp can re-announce on the bus. Set once, before start().
    std::function<void(const StreamState&)> onStateChanged;
};

} // namespace Audio

#endif // AUDIOD_PIPEWIRE_AUDIO_BACKEND_H
