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
    bool setVolumePercent(int percent) override;
    bool setMuted(bool muted) override;
    StreamState state() const override;
    std::vector<Output> outputs() const override;
    bool moveOutputToTarget(const std::string& outputId) override;

private:
    // Connection lifecycle. On drop, retry rather than exit: a phone may bring
    // PipeWire up after this service, and a desktop session can restart.
    void connect();
    void scheduleReconnect();
    static gboolean reconnectCb(gpointer userdata);
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
    static void serverInfoCb(pa_context* c, const pa_server_info* i, void* userdata);
    static void moveResultCb(pa_context* c, int success, void* userdata);
    void refreshStreams();
    void refreshOutputs();
    void publishState();
    void applyDesiredToStreams();

    // Whether a sink is a real output the chooser should offer, as opposed to a
    // monitor, a null/dummy sink, or an effects/loopback virtual sink.
    static bool isSelectableSink(const pa_sink_info* i);

    bool isWebosStream(const pa_sink_input_info* i) const;

    // Per-enumeration token for the async sink list; see m_outputsGeneration.
    struct OutputsScan {
        PulseBackend* self;
        unsigned generation;
    };

    pa_glib_mainloop* m_mainloop = nullptr;
    pa_context* m_context = nullptr;
    bool m_connected = false;
    // The pending reconnect timeout, so it is never double-scheduled and can be
    // cancelled on teardown. 0 means none pending.
    guint m_reconnectSource = 0;

    // The session's sink-input(s). The map is index -> last-known volume, so a
    // change pushed from outside webOS (the host mixer moving our node) is
    // noticed and re-announced.
    std::vector<uint32_t> m_webosSinkInputs;

    // Output enumeration is async: pa_context_get_sink_info_list fires sinkInfoCb
    // once per sink and once more with eol. Several sink events can arrive back
    // to back (a sink, then its port, then its profile), so more than one
    // enumeration can be in flight at once. Each enumeration gets a generation
    // number; its callbacks accumulate into m_outputsScratch, and only the
    // newest generation commits to m_outputs on eol. A stale enumeration's
    // callbacks are dropped. This is what stopped the chooser listing the same
    // output several times.
    unsigned m_outputsGeneration = 0;
    unsigned m_outputsInFlight = 0;
    std::vector<Output> m_outputsScratch;

    // The host's default sink name, from server info; a fallback for "current"
    // when webOS has no stream yet. When it does, the real current output is the
    // sink its sink-input is routed to (m_webosSinkIndex), which wins.
    std::string m_defaultSinkName;
    static const uint32_t kNoSink = (uint32_t)-1;
    uint32_t m_webosSinkIndex = kNoSink;
    // The sink the user chose in the output chooser, applied to a stream that
    // reappears so the choice is not lost. kNoSink means "no explicit choice".
    uint32_t m_desiredSinkIndex = kNoSink;

    // Streams enumeration, same generation guard as outputs: refreshStreams can
    // be re-triggered before an earlier async list finishes, which would append
    // the same sink-input twice.
    unsigned m_streamsGeneration = 0;
    std::vector<uint32_t> m_streamsScratch;
    uint32_t m_streamsScratchSink = kNoSink;
    bool m_streamsScratchPresent = false;
    int m_streamsScratchVolume = 100;
    bool m_streamsScratchMuted = false;
    // The channel count of webOS's stream, so a volume set uses the stream's own
    // layout (mono, stereo, 5.1) rather than assuming two.
    uint8_t m_webosChannels = 2;
    uint8_t m_streamsScratchChannels = 2;

    // The volume/mute webOS should be at, remembered even when there is no stream
    // to apply it to yet. Moving the slider or a key before anything plays sets
    // these; when webOS's stream appears, it is brought to them. This is what
    // makes the slider "stick" instead of doing nothing when webOS is silent.
    bool m_haveDesired = false;
    int m_desiredVolume = 100;
    bool m_desiredMuted = false;

    StreamState m_state;
    std::vector<Output> m_outputs;
};

} // namespace Audio

#endif // AUDIOD_PIPEWIRE_PULSE_BACKEND_H
