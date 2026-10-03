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

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace Audio {

namespace {

// How webOS's own audio nodes are identified. The tricky part, measured live:
// the audio stream does NOT come from the WebAppMgr process itself. QtWebEngine
// spawns a child, QtWebEngineProcess, and that child is what owns the PipeWire
// node -- so application.process.id is the child's PID and
// application.process.binary is "QtWebEngineProcess", neither of which is
// WebAppMgr. What IS stable on that node is application.name / node.name =
// "WebAppManager" (QtWebEngine sets it from the app), and the child's process
// tree, whose ancestor is our WebAppMgr.
//
// So a stream is webOS's when ANY of these holds:
//   * its node/application name is one of ours (the common case for the engine),
//   * its process binary is a webOS binary, or
//   * its process is a descendant of a running WebAppMgr/LunaSysMgr.
// The ancestry check is what keeps a desktop media player (showtime, chrome)
// out while letting QtWebEngineProcess in: that player's tree does not pass
// through our shell.
const char* const kWebosProcNames[] = { "WebAppMgr", "LunaSysMgr", nullptr };
const char* const kWebosNodeNames[] = { "WebAppManager", "LunaSysMgr",
                                        "LunaSysManager", nullptr };
const char* const kWebosBinaries[]  = { "WebAppMgr", "LunaSysMgr",
                                        "QtWebEngineProcess", nullptr };

// Retry cadence when there is no PipeWire to connect to yet. A phone has no
// session at our start; a desktop session can restart. Neither should kill us.
const guint kReconnectSeconds = 2;

bool nameInList(const char* value, const char* const* list)
{
    if (!value)
        return false;
    for (const char* const* p = list; *p; ++p)
        if (std::strcmp(value, *p) == 0)
            return true;
    return false;
}

// Read /proc/<pid>/comm, the kernel's name for the process (truncated to 15).
bool readProcComm(long pid, std::string& out)
{
    char path[64];
    std::snprintf(path, sizeof path, "/proc/%ld/comm", pid);
    std::ifstream in(path);
    if (!in)
        return false;
    std::getline(in, out);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return true;
}

// The parent PID from /proc/<pid>/status.
long readProcPpid(long pid)
{
    char path[64];
    std::snprintf(path, sizeof path, "/proc/%ld/status", pid);
    std::ifstream in(path);
    if (!in)
        return 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("PPid:", 0) == 0) {
            const char* p = line.c_str() + 5;
            while (*p == ' ' || *p == '\t')
                ++p;
            return std::strtol(p, nullptr, 10);
        }
    }
    return 0;
}

// Whether a PID is, or descends from, one of webOS's own processes. Walks up the
// parent chain (bounded) so QtWebEngineProcess -- a child of WebAppMgr -- counts,
// while an unrelated desktop app does not. The bound stops a cycle or a runaway.
bool pidIsWebos(long pid)
{
    for (int hops = 0; pid > 1 && hops < 12; ++hops) {
        std::string comm;
        if (!readProcComm(pid, comm))
            return false;
        if (nameInList(comm.c_str(), kWebosProcNames))
            return true;
        pid = readProcPpid(pid);
    }
    return false;
}

} // namespace

PulseBackend::PulseBackend() = default;

PulseBackend::~PulseBackend()
{
    if (m_reconnectSource != 0) {
        g_source_remove(m_reconnectSource);
        m_reconnectSource = 0;
    }
    if (m_context) {
        pa_context_disconnect(m_context);
        pa_context_unref(m_context);
    }
    if (m_mainloop)
        pa_glib_mainloop_free(m_mainloop);
}

bool PulseBackend::start()
{
    // The glib mainloop is created once and reused across reconnects: a dropped
    // context is replaced, but the mainloop (and the glib source behind it)
    // stays, so there is nothing to free from inside its own callback.
    if (!m_mainloop) {
        m_mainloop = pa_glib_mainloop_new(g_main_context_default());
        if (!m_mainloop) {
            g_warning("audiod-pipewire: pa_glib_mainloop_new failed");
            return false;
        }
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

gboolean PulseBackend::reconnectCb(gpointer userdata)
{
    auto* self = static_cast<PulseBackend*>(userdata);
    self->m_reconnectSource = 0;
    self->connect();
    return G_SOURCE_REMOVE;
}

void PulseBackend::scheduleReconnect()
{
    // Drop the dead context, but keep the mainloop (see start()). Do not free it
    // here: scheduleReconnect runs from inside the context state callback, which
    // runs on that mainloop.
    if (m_context) {
        pa_context_unref(m_context);
        m_context = nullptr;
    }
    m_connected = false;
    // One pending reconnect at a time; a flapping server must not stack timeouts
    // that would each create a context.
    if (m_reconnectSource == 0)
        m_reconnectSource = g_timeout_add_seconds(kReconnectSeconds, &PulseBackend::reconnectCb, this);
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

    // The node/application name set by the engine. On the audio node this is
    // "WebAppManager" (not a content title), so it is a reliable first check.
    const char* appName = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_NAME);
    if (nameInList(appName, kWebosNodeNames))
        return true;
    const char* nodeName = pa_proplist_gets(i->proplist, "node.name");
    if (nameInList(nodeName, kWebosNodeNames))
        return true;

    // The process binary: WebAppMgr/LunaSysMgr, or the QtWebEngineProcess child
    // the engine runs its audio in.
    const char* binary = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_PROCESS_BINARY);
    if (nameInList(binary, kWebosBinaries))
        return true;

    // Last, the process tree: a PID that descends from our shell. This is what
    // keeps a desktop media player out even if it shared a name, and catches a
    // webOS stream whose name/binary were not set.
    const char* pidStr = pa_proplist_gets(i->proplist, PA_PROP_APPLICATION_PROCESS_ID);
    if (pidStr) {
        char* end = nullptr;
        const long pid = std::strtol(pidStr, &end, 10);
        if (end && *end == '\0' && pidIsWebos(pid))
            return true;
    }
    return false;
}

void PulseBackend::refreshStreams()
{
    if (!m_connected || !m_context)
        return;
    // Same generation guard as outputs: a second refresh starting before this
    // one's eol must not let two async lists append to the same accumulator.
    const unsigned gen = ++m_streamsGeneration;
    m_streamsScratch.clear();
    m_streamsScratchSink = kNoSink;
    m_streamsScratchPresent = false;
    m_streamsScratchVolume = m_state.volumePercent;
    m_streamsScratchMuted = m_state.muted;
    OutputsScan* scan = new OutputsScan{ this, gen };
    pa_operation* o =
        pa_context_get_sink_input_info_list(m_context, &PulseBackend::sinkInputInfoCb, scan);
    if (o)
        pa_operation_unref(o);
}

void PulseBackend::sinkInputInfoCb(pa_context*, const pa_sink_input_info* i,
                                   int eol, void* userdata)
{
    auto* scan = static_cast<OutputsScan*>(userdata);
    PulseBackend* self = scan->self;

    if (eol) {
        if (scan->generation == self->m_streamsGeneration) {
            const bool hadStream = !self->m_webosSinkInputs.empty();
            self->m_webosSinkInputs = self->m_streamsScratch;
            self->m_webosSinkIndex = self->m_streamsScratchSink;
            self->m_webosChannels = self->m_streamsScratchChannels;
            self->m_state.present = self->m_streamsScratchPresent;
            self->m_state.volumePercent = self->m_streamsScratchVolume;
            self->m_state.muted = self->m_streamsScratchMuted;
            // A webOS stream that just appeared is brought to the level the user
            // already set on the slider while webOS was silent.
            if (!hadStream && !self->m_webosSinkInputs.empty() && self->m_haveDesired) {
                self->applyDesiredToStreams();
                self->m_state.volumePercent = self->m_desiredVolume;
                self->m_state.muted = self->m_desiredMuted;
            }
            self->publishState();
            // The current output depends on which sink webOS is routed to, so a
            // stream change can change the chooser's marking too.
            self->refreshOutputs();
        }
        delete scan;
        return;
    }

    if (scan->generation != self->m_streamsGeneration)
        return;
    if (!self->isWebosStream(i))
        return;

    self->m_streamsScratch.push_back(i->index);
    // The sink this stream is routed to is "where webOS's audio goes" -- the
    // output the chooser should mark current.
    self->m_streamsScratchSink = i->sink;

    // The session's own volume and mute, read back from the node QtWebEngine
    // created. pa_cvolume_avg over the channels gives the single scalar webOS's
    // 0..100 maps onto.
    const double linear = static_cast<double>(pa_cvolume_avg(&i->volume)) / PA_VOLUME_NORM;
    self->m_streamsScratchPresent = true;
    self->m_streamsScratchVolume = AudioContract::volumeToPercent(linear);
    self->m_streamsScratchMuted = i->mute != 0;
    self->m_streamsScratchChannels = i->volume.channels ? i->volume.channels : 2;
}

bool PulseBackend::setVolumePercent(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    // Remember it as the level webOS should be at, whether or not there is a
    // stream right now. applyDesiredToStreams() brings a stream to it the moment
    // one appears, so moving the slider while webOS is silent is not lost.
    m_haveDesired = true;
    m_desiredVolume = percent;
    m_state.volumePercent = percent; // optimistic echo; corrected by read-back

    if (!m_connected || !m_context || m_webosSinkInputs.empty())
        return true; // accepted and remembered, applied when a stream exists

    pa_volume_t v = static_cast<pa_volume_t>(
        AudioContract::percentToVolume(percent) * PA_VOLUME_NORM + 0.5);
    for (uint32_t idx : m_webosSinkInputs) {
        pa_cvolume cv;
        // Across the stream's own channels, one level on each: webOS presents a
        // single fader, so mono/stereo/5.1 all take the same per-channel volume
        // -- but with the stream's real channel count, not an assumed two.
        pa_cvolume_set(&cv, m_webosChannels ? m_webosChannels : 2, v);
        pa_operation* o =
            pa_context_set_sink_input_volume(m_context, idx, &cv, nullptr, nullptr);
        if (o)
            pa_operation_unref(o);
    }
    return true;
}

bool PulseBackend::setMuted(bool muted)
{
    m_haveDesired = true;
    m_desiredMuted = muted;
    m_state.muted = muted;

    if (!m_connected || !m_context || m_webosSinkInputs.empty())
        return true;
    for (uint32_t idx : m_webosSinkInputs) {
        pa_operation* o =
            pa_context_set_sink_input_mute(m_context, idx, muted ? 1 : 0, nullptr, nullptr);
        if (o)
            pa_operation_unref(o);
    }
    return true;
}

// Bring a freshly-appeared webOS stream to the remembered level, so the slider's
// position before anything played is honored once it does.
void PulseBackend::applyDesiredToStreams()
{
    if (!m_haveDesired || m_webosSinkInputs.empty() || !m_connected || !m_context)
        return;
    pa_volume_t v = static_cast<pa_volume_t>(
        AudioContract::percentToVolume(m_desiredVolume) * PA_VOLUME_NORM + 0.5);
    for (uint32_t idx : m_webosSinkInputs) {
        pa_cvolume cv;
        pa_cvolume_set(&cv, m_webosChannels ? m_webosChannels : 2, v);
        pa_operation* o =
            pa_context_set_sink_input_volume(m_context, idx, &cv, nullptr, nullptr);
        if (o)
            pa_operation_unref(o);
        pa_operation* om =
            pa_context_set_sink_input_mute(m_context, idx, m_desiredMuted ? 1 : 0, nullptr, nullptr);
        if (om)
            pa_operation_unref(om);
    }
}

StreamState PulseBackend::state() const
{
    return m_state;
}

void PulseBackend::refreshOutputs()
{
    if (!m_connected || !m_context)
        return;
    // First the default sink name, then the sink list. The server info callback
    // kicks off the sink enumeration, so "current" can be decided against a
    // default that is already known.
    pa_operation* o = pa_context_get_server_info(m_context, &PulseBackend::serverInfoCb, this);
    if (o)
        pa_operation_unref(o);
}

void PulseBackend::serverInfoCb(pa_context* c, const pa_server_info* i, void* userdata)
{
    auto* self = static_cast<PulseBackend*>(userdata);
    if (i && i->default_sink_name)
        self->m_defaultSinkName = i->default_sink_name;

    // Begin a fresh enumeration generation. Its callbacks accumulate into
    // m_outputsScratch and only this generation commits on eol; any enumeration
    // already in flight becomes stale and is dropped. The scan token carries the
    // generation and is freed on eol.
    const unsigned gen = ++self->m_outputsGeneration;
    self->m_outputsScratch.clear();
    OutputsScan* scan = new OutputsScan{ self, gen };
    pa_operation* o = pa_context_get_sink_info_list(c, &PulseBackend::sinkInfoCb, scan);
    if (o)
        pa_operation_unref(o);
}

// A sink the chooser should offer: a real output, not a monitor, a null/dummy
// sink, or an effects/loopback virtual sink. Monitors are sources and do not
// appear here; this screens the virtual sinks that do.
bool PulseBackend::isSelectableSink(const pa_sink_info* i)
{
    if (!i || !i->name)
        return false;
    // Dummy/null output: PipeWire's placeholder when nothing real is present.
    if (std::strstr(i->name, "auto_null") || std::strstr(i->name, "dummy"))
        return false;
    // Effects/loopback virtual sinks route into another sink; offering them
    // would list the same speakers twice. PipeWire tags its own loopback and
    // filter nodes; screen the common ones by name.
    if (i->proplist) {
        const char* nodeName = pa_proplist_gets(i->proplist, "node.name");
        if (nodeName && (std::strstr(nodeName, "effect")
                         || std::strstr(nodeName, "Effects")
                         || std::strstr(nodeName, "loopback")))
            return false;
    }
    return true;
}

void PulseBackend::sinkInfoCb(pa_context*, const pa_sink_info* i, int eol, void* userdata)
{
    auto* scan = static_cast<OutputsScan*>(userdata);
    PulseBackend* self = scan->self;

    if (eol) {
        // Only the newest enumeration commits; a stale one (a newer refresh
        // started while this was in flight) is dropped so its sinks cannot be
        // appended on top of the current list -- which is what listed the same
        // output several times.
        if (scan->generation == self->m_outputsGeneration) {
            self->m_outputs = self->m_outputsScratch;
            self->publishState();
        }
        delete scan;
        return;
    }

    if (scan->generation != self->m_outputsGeneration)
        return; // stale; wait for its eol to clean up
    if (!isSelectableSink(i))
        return;

    Output out;
    out.id = std::to_string(i->index);
    // The label the chooser lists. Prefer node.nick -- the short, distinctive
    // name PipeWire assigns ("Speaker", "HDMI 1") -- over the description, which
    // repeats the chipset ("500 Series Chipset Family HD Audio ...") on every
    // port of the same card and reads as the same device several times. Fall
    // back to the description, then the internal name.
    const char* nick = i->proplist ? pa_proplist_gets(i->proplist, "node.nick") : nullptr;
    if (nick && *nick)
        out.name = nick;
    else if (i->description && *i->description)
        out.name = i->description;
    else
        out.name = i->name ? i->name : "";
    // "current": the sink webOS's own stream is routed to when it has one;
    // otherwise the host default, so the chooser still marks something sensible.
    if (self->m_webosSinkIndex != kNoSink)
        out.isDefault = (i->index == self->m_webosSinkIndex);
    else
        out.isDefault = (i->name && self->m_defaultSinkName == i->name);
    self->m_outputsScratch.push_back(std::move(out));
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
