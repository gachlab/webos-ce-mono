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
// com.palm.audio, answered over PipeWire through the PulseAudio client API.
//
// On a device this was audiod. Nothing in the CE drop provides it, so every
// audio control in the shell is inert: Mute Sound round-trips through db8 and is
// consumed by no one, the volume HUD never draws, the volume keys reach the
// shell and nothing acts on them, and the 12 files in /usr/palm/sounds never
// play. The apps' own audio works, because QtWebEngine makes its own PipeWire
// node; it is the *shell's* audio service that is missing. This provides it.
//
// The bus contract, verified against the two consumers still in the tree
// (NativeAlertManager and DisplayManager) and against the shell's publishers
// (InputManager, SoundPlayerPool); see audio_contract.h for the exact payloads.
//
//   IN   com.palm.keys/audio/status      subscribe; {"key":"volume_up"|
//                                         "volume_down","state":"up"}
//        com.palm.systemservice/getPreferences  subscribe; muteSound (+ the tone
//                                         names, read for playFeedback defaults)
//
//   OUT  com.palm.audio/<category>/status subscribe-served; the volume HUD and
//                                         DisplayManager read these
//        systemsounds/playFeedback        served; plays a shipped file as webOS's
//                                         own PipeWire stream
//
// The engine is behind Audio::Backend (audio_backend.h) so this file never names
// a sound library. Volume and mute are proven and implemented; the output
// chooser's move is left unimplemented pending the PipeWire-vs-WirePlumber
// measurement -- see pulse_backend.cpp.
//

#include "audio_contract.h"
#include "json_lite.h"
#include "pulse_backend.h"
#include "system_sounds.h"

#include <luna-service2/lunaservice.h>

#include <glib.h>
#include <glib-unix.h>

#include <memory>
#include <string>

namespace {

const char kServiceName[] = "com.palm.audio";
const char kDefaultSoundsDir[] = "/usr/palm/sounds";

GMainLoop* g_loop = nullptr;
LSPalmService* g_service = nullptr;
std::unique_ptr<Audio::Backend> g_backend;

// The categories the shell subscribes to, each a /<name>/status method that
// holds subscriptions and gets an announcement whenever state changes.
const AudioContract::Category kCategories[] = {
    AudioContract::Category::System,
    AudioContract::Category::Media,
    AudioContract::Category::Ringtone,
    AudioContract::Category::Phone,
};

LSHandle* privateBus()
{
    return LSPalmServiceGetPrivateConnection(g_service);
}

void logAndFree(const char* where, LSError& error)
{
    g_warning("audiod-pipewire: %s: %s", where, error.message ? error.message : "(no message)");
    LSErrorFree(&error);
}

bool reply(LSHandle* sh, LSMessage* message, const std::string& payload)
{
    LSError error;
    LSErrorInit(&error);
    if (!LSMessageReply(sh, message, payload.c_str(), &error))
        logAndFree("LSMessageReply", error);
    return true;
}

// --- serving com.palm.audio/<category>/status -------------------------------

// The subscription key is the category path, so an announcement for one category
// reaches exactly its subscribers. categoryStatus adds the caller to that key
// and answers with the current state as a plain (non-HUD) carry.
bool addStatusSubscription(LSHandle* sh, LSMessage* message, AudioContract::Category category)
{
    if (!LSMessageIsSubscription(message))
        return false;
    LSError error;
    LSErrorInit(&error);
    LSSubscriptionAdd(sh, AudioContract::categoryPath(category), message, &error);
    if (LSErrorIsSet(&error)) {
        logAndFree("LSSubscriptionAdd", error);
        return false;
    }
    return true;
}

std::string currentPayload(AudioContract::Category category, bool withChangedVolume,
                           bool withReturnValue)
{
    const Audio::StreamState s = g_backend->state();
    return AudioContract::statusPayload(AudioContract::defaultScenario(category),
                                        s.volumePercent, s.muted, withChangedVolume,
                                        withReturnValue);
}

// One method body for all four categories; the category is carried in the
// method's userdata so the table entries differ only by that pointer.
bool statusMethod(LSHandle* sh, LSMessage* message, void* ctx)
{
    const auto category = static_cast<AudioContract::Category>(reinterpret_cast<intptr_t>(ctx));
    addStatusSubscription(sh, message, category);
    // The first reply is a state carry, not a HUD trigger: a fresh subscriber
    // learning the level must not pop the alert window.
    return reply(sh, message, currentPayload(category, /*withChangedVolume=*/false,
                                             /*withReturnValue=*/true));
}

// Announce to a category's subscribers. withChangedVolume=true pops the HUD.
void announce(AudioContract::Category category, bool withChangedVolume)
{
    LSError error;
    LSErrorInit(&error);
    const std::string payload = currentPayload(category, withChangedVolume,
                                               /*withReturnValue=*/false);
    if (!LSSubscriptionReply(privateBus(), AudioContract::categoryPath(category),
                             payload.c_str(), &error))
        logAndFree("LSSubscriptionReply", error);
}

void announceAll(bool withChangedVolume)
{
    for (AudioContract::Category c : kCategories)
        announce(c, withChangedVolume);
}

// A volume/mute change belongs to one category (system): announcing it to all
// four made NativeAlertManager pop the HUD four times for one key press
// (MEASURED). The HUD draws on the system scenario, so one announce suffices,
// and system_default is the honest scenario for webOS's single fader -- it is
// its own UI volume, not a per-media-stream volume, so the system HUD art is
// the right one, not a race between four scenarios for which draws last.
//
// This narrows the volume announcement to the system category. The only other
// consumer, AudioMenuBridge, subscribes to system/status, so it still sees the
// change; media/ringtone/phone subscribers (none today) would not get a volume
// update this way. onStateChanged still refreshes all four with announceAll on a
// backend-observed change, which is the path that keeps every category's last
// state current.
void announceVolumeChange()
{
    announce(AudioContract::Category::System, /*withChangedVolume=*/true);
}

// --- systemsounds/playFeedback ----------------------------------------------

std::string soundsDir()
{
    const char* env = std::getenv("WEBOS_SYSTEM_SOUNDS");
    if (env && *env)
        return env;
    return kDefaultSoundsDir;
}

// Reap a finished pw-play so it does not linger as a zombie. g_spawn_async with
// G_SPAWN_DO_NOT_REAP_CHILD hands us the pid; this watch closes it on exit.
void onSoundChildExit(GPid pid, gint /*status*/, gpointer /*data*/)
{
    g_spawn_close_pid(pid);
}

// Play one shipped file as webOS's own PipeWire stream. Spawned rather than
// decoded in-process: pw-play reads wav and mp3 and routes through PipeWire with
// its own node, which is exactly "webOS's audio" -- no decoder pulled into this
// service, and the sound lands under the same per-application volume. A name
// with no shipped file plays nothing.
void playSound(const std::string& name)
{
    const std::string path = SystemSounds::pathForName(soundsDir(), name);
    if (path.empty()) {
        g_debug("audiod-pipewire: playFeedback '%s' has no shipped file, ignoring",
                name.c_str());
        return;
    }
    const char* argv[] = { "pw-play", path.c_str(), nullptr };
    GError* gerror = nullptr;
    GPid pid = 0;
    // DO_NOT_REAP_CHILD so a child watch can collect the finished player; without
    // it a shell that plays feedback often would leak zombie pw-play processes.
    if (!g_spawn_async(nullptr, const_cast<char**>(argv), nullptr,
                       static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH
                                                | G_SPAWN_DO_NOT_REAP_CHILD
                                                | G_SPAWN_STDOUT_TO_DEV_NULL
                                                | G_SPAWN_STDERR_TO_DEV_NULL),
                       nullptr, nullptr, &pid, &gerror)) {
        g_warning("audiod-pipewire: could not play %s: %s", path.c_str(),
                  gerror ? gerror->message : "(no message)");
        if (gerror)
            g_error_free(gerror);
        return;
    }
    g_child_watch_add(pid, onSoundChildExit, nullptr);
}

bool playFeedback(LSHandle* sh, LSMessage* message, void*)
{
    JsonLite::Document doc(LSMessageGetPayload(message));
    if (auto name = JsonLite::getString(doc.root(), "name"))
        playSound(*name);
    return reply(sh, message, "{\"returnValue\":true}");
}

// setVolume {"volume": int}: webOS's own volume, from the slider in the menu.
// Applied to the session's streams and re-announced so the HUD and any other
// subscriber stay in step. Served on the system category.
bool setVolumeMethod(LSHandle* sh, LSMessage* message, void*)
{
    JsonLite::Document doc(LSMessageGetPayload(message));
    bool applied = false;
    if (auto v = JsonLite::getInt(doc.root(), "volume")) {
        int percent = *v;
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;
        applied = g_backend->setVolumePercent(percent);
        if (applied)
            announceVolumeChange();
    }
    const std::string payload = std::string("{\"returnValue\":")
        + (applied ? "true" : "false") + ",\"applied\":" + (applied ? "true" : "false") + "}";
    return reply(sh, message, payload);
}

// --- the output chooser's list ----------------------------------------------

// The outputs payload: {"returnValue":true,"outputs":[{id,name,current},...]}.
// Built from the backend's view of the host's sinks. "current" marks the one
// webOS is on now; until moveOutputToTarget is settled (the PipeWire-vs-
// WirePlumber measurement), that is whichever the host's policy chose for our
// node, and selecting another does not change it -- the service answers the
// selection but the list keeps marking the real one.
std::string outputsPayload(bool withReturnValue)
{
    std::string p = "{";
    if (withReturnValue)
        p += "\"returnValue\":true,";
    p += "\"outputs\":[";
    const std::vector<Audio::Output> outs = g_backend->outputs();
    for (size_t i = 0; i < outs.size(); ++i) {
        if (i)
            p += ",";
        p += AudioContract::outputEntry(outs[i].id, outs[i].name, outs[i].isDefault);
    }
    p += "]}";
    return p;
}

const char kOutputsKey[] = "outputs";

// listOutputs: subscribe to the set of outputs. Answered now, and re-pushed to
// subscribers whenever the backend's sink list changes.
bool listOutputs(LSHandle* sh, LSMessage* message, void*)
{
    if (LSMessageIsSubscription(message)) {
        LSError error;
        LSErrorInit(&error);
        LSSubscriptionAdd(sh, kOutputsKey, message, &error);
        if (LSErrorIsSet(&error))
            logAndFree("LSSubscriptionAdd outputs", error);
    }
    return reply(sh, message, outputsPayload(/*withReturnValue=*/true));
}

void announceOutputs()
{
    LSError error;
    LSErrorInit(&error);
    if (!LSSubscriptionReply(privateBus(), kOutputsKey,
                             outputsPayload(/*withReturnValue=*/false).c_str(), &error))
        logAndFree("LSSubscriptionReply outputs", error);
}

// selectOutput {"id": string}: send webOS's audio to the chosen output. The
// backend's move is unimplemented pending the measurement, so this reports
// honestly: returnValue reflects whether the move was actually applied.
bool selectOutput(LSHandle* sh, LSMessage* message, void*)
{
    JsonLite::Document doc(LSMessageGetPayload(message));
    bool moved = false;
    if (auto id = JsonLite::getString(doc.root(), "id"))
        moved = g_backend->moveOutputToTarget(*id);
    const std::string reply_payload =
        std::string("{\"returnValue\":") + (moved ? "true" : "false")
        + ",\"applied\":" + (moved ? "true" : "false") + "}";
    return reply(sh, message, reply_payload);
}

// --- consuming the shell's publishers ---------------------------------------

// com.palm.keys/audio/status: {"key":"volume_up"|"volume_down","state":"up"}.
// Only "up" is ever published (the press half never arrives), so each event is
// one step; acting on it moves the volume and pops the HUD.
bool onVolumeKey(LSHandle*, LSMessage* message, void*)
{
    JsonLite::Document doc(LSMessageGetPayload(message));
    if (auto key = JsonLite::getString(doc.root(), "key")) {
        const int now = g_backend->state().volumePercent;
        const int next = AudioContract::applyVolumeKey(now, *key);
        if (next != now) {
            if (g_backend->setVolumePercent(next))
                announceVolumeChange();
        }
    }
    return true;
}

// com.palm.systemservice/getPreferences subscribe: muteSound carries the shell's
// Mute Sound toggle, the one that round-trips through db8 and nobody consumed.
bool onPreferences(LSHandle*, LSMessage* message, void*)
{
    JsonLite::Document doc(LSMessageGetPayload(message));
    if (auto muted = JsonLite::getBool(doc.root(), "muteSound")) {
        if (*muted != g_backend->state().muted) {
            if (g_backend->setMuted(*muted))
                announceVolumeChange();
        }
    }
    return true;
}

void subscribeToShellPublishers()
{
    LSError error;
    LSErrorInit(&error);
    if (!LSCall(privateBus(), "palm://com.palm.keys/audio/status",
                "{\"subscribe\":true}", onVolumeKey, nullptr, nullptr, &error))
        logAndFree("subscribe com.palm.keys/audio/status", error);

    LSErrorInit(&error);
    if (!LSCall(privateBus(), "palm://com.palm.systemservice/getPreferences",
                "{\"subscribe\":true,\"keys\":[\"muteSound\",\"alerttone\","
                "\"notificationtone\",\"ringtone\"]}",
                onPreferences, nullptr, nullptr, &error))
        logAndFree("subscribe com.palm.systemservice/getPreferences", error);
}

// --- registration ------------------------------------------------------------

LSMethod kSystemMethods[]    = { { "status", statusMethod }, { "setVolume", setVolumeMethod }, { } };
LSMethod kMediaMethods[]     = { { "status", statusMethod }, { } };
LSMethod kRingtoneMethods[]  = { { "status", statusMethod }, { } };
LSMethod kPhoneMethods[]     = { { "status", statusMethod }, { } };
LSMethod kSysSoundsMethods[] = { { "playFeedback", playFeedback }, { } };
LSMethod kOutputsMethods[]   = { { "listOutputs", listOutputs }, { "selectOutput", selectOutput }, { } };

bool registerCategory(const char* category, LSMethod* methods, void* ctx)
{
    LSError error;
    LSErrorInit(&error);
    if (!LSPalmServiceRegisterCategory(g_service, category, methods, nullptr, nullptr,
                                       ctx, &error)) {
        logAndFree(category, error);
        return false;
    }
    return true;
}

void* categoryCtx(AudioContract::Category c)
{
    return reinterpret_cast<void*>(static_cast<intptr_t>(c));
}

gboolean quit(gpointer)
{
    g_main_loop_quit(g_loop);
    return G_SOURCE_REMOVE;
}

} // namespace

int main()
{
    g_loop = g_main_loop_new(nullptr, FALSE);

    LSError error;
    LSErrorInit(&error);
    if (!LSRegisterPalmService(kServiceName, &g_service, &error)) {
        logAndFree("LSRegisterPalmService", error);
        return 1;
    }

    if (!registerCategory("/system", kSystemMethods, categoryCtx(AudioContract::Category::System))
        || !registerCategory("/media", kMediaMethods, categoryCtx(AudioContract::Category::Media))
        || !registerCategory("/ringtone", kRingtoneMethods, categoryCtx(AudioContract::Category::Ringtone))
        || !registerCategory("/phone", kPhoneMethods, categoryCtx(AudioContract::Category::Phone))
        || !registerCategory("/systemsounds", kSysSoundsMethods, nullptr)
        || !registerCategory("/outputs", kOutputsMethods, nullptr))
        return 1;

    LSErrorInit(&error);
    if (!LSGmainAttachPalmService(g_service, g_loop, &error)) {
        logAndFree("LSGmainAttachPalmService", error);
        return 1;
    }

    g_backend = std::make_unique<Audio::PulseBackend>();
    // When the engine sees the stream change (ours or the host's mixer moving
    // our node), re-announce so the HUD and the slider stay in step. A change
    // from outside webOS is a state carry, not a key press, so it does not pop
    // the HUD -- withChangedVolume=false.
    g_backend->onStateChanged = [](const Audio::StreamState&) {
        announceAll(/*withChangedVolume=*/false);
        announceOutputs();
    };
    if (!g_backend->start())
        g_warning("audiod-pipewire: audio backend did not start; will keep serving the bus");

    subscribeToShellPublishers();

    g_unix_signal_add(SIGTERM, quit, nullptr);
    g_unix_signal_add(SIGINT, quit, nullptr);

    g_message("audiod-pipewire: com.palm.audio up");
    g_main_loop_run(g_loop);

    LSErrorInit(&error);
    if (!LSUnregisterPalmService(g_service, &error))
        logAndFree("LSUnregisterPalmService", error);
    g_main_loop_unref(g_loop);
    return 0;
}
