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

#ifndef AUDIOD_PIPEWIRE_PULSE_BACKEND_H
#define AUDIOD_PIPEWIRE_PULSE_BACKEND_H

//
// The one Backend, over the PulseAudio client API -- which is the right choice
// here, not a workaround, and the reason is worth stating because the ticket's
// title ("on PipeWire") and this file's API ("pulse") look like a contradiction
// and are not:
//
//   * The PulseAudio *daemon* is dead and is not running. The daemon here is
//     PipeWire.
//   * The PulseAudio *client API* is one of the APIs PipeWire implements, and is
//     the one its own documentation points ordinary applications at, over the
//     native pipewire API. The measurement agrees: the session's own stream
//     already arrives tagged client.api="pipewire-pulse". So talking Pulse here
//     is talking to PipeWire, through the interface PipeWire wants apps to use.
//   * Sink-inputs are per-application by construction, which is exactly the unit
//     this service needs: set_sink_input_volume IS "webOS's volume", scoped to
//     webOS's node and nothing else on the machine.
//
// WirePlumber -- the session manager that usually sits on top of PipeWire -- does
// not enter the volume/mute path at all: per-stream volume is PipeWire's, not
// policy's. It only becomes the question for moveOutputToTarget(), which is why
// that one method is unimplemented until measured. See pulse_backend.cpp.
//
// Uses pa_glib_mainloop so the whole service is single-threaded on the one glib
// loop luna-service2 is already attached to, the same loop model as the rest of
// the services tree. No pa_threaded_mainloop, no locking.
//

#include "audio_backend.h"

#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

#include <map>
#include <string>
#include <vector>

namespace Audio {

class PulseBackend : public Backend {
public:
    PulseBackend();
    ~PulseBackend() override;

    bool start() override;
    void setVolumePercent(int percent) override;
    void setMuted(bool muted) override;
    StreamState state() const override;
    std::vector<Output> outputs() const override;
    bool moveOutputToTarget(const std::string& outputId) override;

private:
    // Connection lifecycle. On drop, retry rather than exit: a phone may bring
    // PipeWire up after this service, and a desktop session can restart.
    void connect();
    void scheduleReconnect();
    static void contextStateCb(pa_context* c, void* userdata);
    static void subscribeCb(pa_context* c, pa_subscription_event_type_t t,
                            uint32_t idx, void* userdata);

    // Finding and tracking webOS's own sink-input. "webOS's stream" is the one
    // whose application.name is WebAppManager (QtWebEngine's node); system
    // sounds will add LunaSysMgr's, grouped under the same volume here.
    static void sinkInputInfoCb(pa_context* c, const pa_sink_input_info* i,
                                int eol, void* userdata);
    static void sinkInfoCb(pa_context* c, const pa_sink_info* i,
                           int eol, void* userdata);
    void refreshStreams();
    void refreshOutputs();
    void publishState();

    bool isWebosStream(const pa_sink_input_info* i) const;

    pa_glib_mainloop* m_mainloop = nullptr;
    pa_context* m_context = nullptr;
    bool m_connected = false;

    // The session's sink-input(s). The map is index -> last-known volume, so a
    // change pushed from outside webOS (the host mixer moving our node) is
    // noticed and re-announced.
    std::vector<uint32_t> m_webosSinkInputs;

    StreamState m_state;
    std::vector<Output> m_outputs;
};

} // namespace Audio

#endif // AUDIOD_PIPEWIRE_PULSE_BACKEND_H
