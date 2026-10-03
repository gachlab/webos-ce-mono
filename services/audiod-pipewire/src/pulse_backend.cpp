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

#include "pulse_backend.h"
#include "audio_contract.h"

#include <glib.h>

#include <cstring>

namespace Audio {

namespace {

// The application.name PipeWire gives the browser engine's node. MEASURED on the
// running session. System sounds (LunaSysMgr) will add a second node; both are
// treated as "webOS's audio" and share one volume, which is the single-fader
// answer to the ticket's open question -- revisit if a second fader is wanted.
const char kWebosAppName[] = "WebAppManager";
const char kWebosAppNameSysmgr[] = "LunaSysMgr";

// Retry cadence when there is no PipeWire to connect to yet. A phone has no
// session at our start; a desktop session can restart. Neither should kill us.
const guint kReconnectSeconds = 2;

bool nameMatches(const char* value, const char* want)
{
    return value && std::strcmp(value, want) == 0;
}

} // namespace

PulseBackend::PulseBackend() = default;

PulseBackend::~PulseBackend()
{
    if (m_context) {
        pa_context_disconnect(m_context);
        pa_context_unref(m_context);
    }
    if (m_mainloop)
        pa_glib_mainloop_free(m_mainloop);
}

bool PulseBackend::start()
{
    m_mainloop = pa_glib_mainloop_new(g_main_context_default());
    if (!m_mainloop) {
        g_warning("audiod-pipewire: pa_glib_mainloop_new failed");
        return false;
    }
    connect();
    return true;
}

void PulseBackend::connect()
{
    pa_mainloop_api* api = pa_glib_mainloop_get_api(m_mainloop);
    m_context = pa_context_new(api, "com.palm.audio");
    if (!m_context) {
        g_warning("audiod-pipewire: pa_context_new failed");
        scheduleReconnect();
        return;
    }
    pa_context_set_state_callback(m_context, &PulseBackend::contextStateCb, this);
    if (pa_context_connect(m_context, nullptr, PA_CONTEXT_NOFAIL, nullptr) < 0) {
        g_warning("audiod-pipewire: pa_context_connect failed: %s",
                  pa_strerror(pa_context_errno(m_context)));
        scheduleReconnect();
    }
}

namespace {
gboolean reconnectCb(gpointer userdata)
{
    static_cast<PulseBackend*>(userdata)->start();
    return G_SOURCE_REMOVE;
}
} // namespace

void PulseBackend::scheduleReconnect()
{
    if (m_context) {
        pa_context_unref(m_context);
        m_context = nullptr;
    }
    // start() re-creates the mainloop too; free the old one first.
    if (m_mainloop) {
        pa_glib_mainloop_free(m_mainloop);
        m_mainloop = nullptr;
    }
    g_timeout_add_seconds(kReconnectSeconds, reconnectCb, this);
}

void PulseBackend::contextStateCb(pa_context* c, void* userdata)
{
    auto* self = static_cast<PulseBackend*>(userdata);
    switch (pa_context_get_state(c)) {
    case PA_CONTEXT_READY: {
        self->m_connected = true;
        g_message("audiod-pipewire: connected to the audio server");
        pa_context_set_subscribe_callback(c, &PulseBackend::subscribeCb, self);
        pa_operation* o = pa_context_subscribe(
            c,
            static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SINK_INPUT
                                                | PA_SUBSCRIPTION_MASK_SINK),
            nullptr, nullptr);
        if (o)
            pa_operation_unref(o);
        self->refreshStreams();
        self->refreshOutputs();
        break;
    }
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        g_warning("audiod-pipewire: connection lost, will retry");
        self->m_connected = false;
        self->m_state.present = false;
        self->publishState();
        self->scheduleReconnect();
        break;
    default:
        break;
    }
}

void PulseBackend::subscribeCb(pa_context*, pa_subscription_event_type_t t,
                               uint32_t, void* userdata)
{
    auto* self = static_cast<PulseBackend*>(userdata);
    const unsigned facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    if (facility == PA_SUBSCRIPTION_EVENT_SINK_INPUT)
        self->refreshStreams();
    else if (facility == PA_SUBSCRIPTION_EVENT_SINK)
        self->refreshOutputs();
}

bool PulseBackend::isWebosStream(const pa_sink_input_info* i) const
{
    if (!i || !i->proplist)
        return false;
    const char* app = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME);
    return nameMatches(app, kWebosAppName) || nameMatches(app, kWebosAppNameSysmgr);
}

void PulseBackend::refreshStreams()
{
    if (!m_connected || !m_context)
        return;
    // Rebuilt from scratch each pass: the callback appends, so clear the
    // accumulator first. publishState runs on eol.
    m_webosSinkInputs.clear();
    pa_operation* o =
        pa_context_get_sink_input_info_list(m_context, &PulseBackend::sinkInputInfoCb, this);
    if (o)
        pa_operation_unref(o);
}

void PulseBackend::sinkInputInfoCb(pa_context*, const pa_sink_input_info* i,
                                   int eol, void* userdata)
{
    auto* self = static_cast<PulseBackend*>(userdata);
    if (eol) {
        self->publishState();
        return;
    }
    if (!self->isWebosStream(i))
        return;

    self->m_webosSinkInputs.push_back(i->index);

    // The session's own volume and mute, read back from the node QtWebEngine
    // created. pa_cvolume_avg over the channels gives the single scalar webOS's
    // 0..100 maps onto.
    const double linear = static_cast<double>(pa_cvolume_avg(&i->volume)) / PA_VOLUME_NORM;
    self->m_state.present = true;
    self->m_state.volumePercent = AudioContract::volumeToPercent(linear);
    self->m_state.muted = i->mute != 0;
}

void PulseBackend::setVolumePercent(int percent)
{
    if (!m_connected || !m_context || m_webosSinkInputs.empty())
        return;
    pa_volume_t v = static_cast<pa_volume_t>(
        AudioContract::percentToVolume(percent) * PA_VOLUME_NORM + 0.5);
    for (uint32_t idx : m_webosSinkInputs) {
        pa_cvolume cv;
        // Mono-spread across the node's channels; the browser node is stereo but
        // webOS presents one fader, so both channels take the same level.
        pa_cvolume_set(&cv, 2, v);
        pa_operation* o =
            pa_context_set_sink_input_volume(m_context, idx, &cv, nullptr, nullptr);
        if (o)
            pa_operation_unref(o);
    }
    m_state.volumePercent = percent;
}

void PulseBackend::setMuted(bool muted)
{
    if (!m_connected || !m_context || m_webosSinkInputs.empty())
        return;
    for (uint32_t idx : m_webosSinkInputs) {
        pa_operation* o =
            pa_context_set_sink_input_mute(m_context, idx, muted ? 1 : 0, nullptr, nullptr);
        if (o)
            pa_operation_unref(o);
    }
    m_state.muted = muted;
}

StreamState PulseBackend::state() const
{
    return m_state;
}

void PulseBackend::refreshOutputs()
{
    if (!m_connected || !m_context)
        return;
    m_outputs.clear();
    pa_operation* o = pa_context_get_sink_info_list(m_context, &PulseBackend::sinkInfoCb, this);
    if (o)
        pa_operation_unref(o);
}

void PulseBackend::sinkInfoCb(pa_context*, const pa_sink_info* i, int eol, void* userdata)
{
    auto* self = static_cast<PulseBackend*>(userdata);
    if (eol) {
        // The output list is part of what main.cpp announces on a state change;
        // a sink appearing or leaving is such a change.
        self->publishState();
        return;
    }
    Output out;
    out.id = std::to_string(i->index);
    // The real device label, as the chooser must list it -- description, not the
    // internal name (e.g. "Built-in Audio Analog Stereo", not "alsa_output...").
    out.name = i->description ? i->description : (i->name ? i->name : "");
    self->m_outputs.push_back(std::move(out));
}

std::vector<Output> PulseBackend::outputs() const
{
    return m_outputs;
}

bool PulseBackend::moveOutputToTarget(const std::string& outputId)
{
    // UNIMPLEMENTED until the PipeWire-vs-WirePlumber question is MEASURED. The
    // chooser's UI, the bus contract and the volume half do not depend on this.
    //
    // What is known so far (from the ticket's measurement): writing the stream's
    // target.object as a node name did nothing, and as an object.serial it was
    // reverted within three seconds. Two candidate causes, needing one
    // observation to tell apart:
    //
    //   (a) the write never lands  -> a PipeWire/command problem (wrong metadata
    //       store, wrong subject id, or permissions);
    //   (b) the write lands and the session manager reverts it -> a WirePlumber
    //       /policy problem (the custom Lua preferred-device component).
    //
    // The settling test, to run when an audible change is acceptable (it moves
    // real audio between real outputs): issue the move, then read the stream's
    // route back immediately, at 1s and at 3s. Appears-then-reverts is (b);
    // never-appears is (a). Only after that is the path chosen, from:
    //
    //   1. pa_context_move_sink_input_by_index(m_context, sinkInputIdx,
    //      atoi(outputId), ...) -- the PulseAudio move, untried so far and the
    //      most in keeping with the pipewire-pulse route the session already
    //      uses. Likely the answer for (a).
    //   2. PipeWire metadata target.object with the correct subject/serial form.
    //   3. Asking WirePlumber, if (b) and it refuses to yield -- which couples us
    //      to a session manager the phone case may not have.
    //
    // Returning false keeps the chooser honest: it can list outputs and show the
    // current one; selecting a new one reports "not yet" rather than silently
    // doing nothing.
    (void)outputId;
    g_message("audiod-pipewire: moveOutputToTarget is not implemented yet "
              "(pending the PipeWire-vs-WirePlumber measurement)");
    return false;
}

void PulseBackend::publishState()
{
    if (onStateChanged)
        onStateChanged(m_state);
}

} // namespace Audio
