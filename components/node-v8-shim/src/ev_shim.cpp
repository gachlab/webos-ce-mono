/* Copyright (c) 2026 webOS CE modern build
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
 */

#include "ev.h"
#include "v8.h"

#include <cstring>
#include <map>
#include <cstdio>
#include <cstdlib>
// SHIM_TRACE=1 prints what the loop bridge is doing.
#define TRACE(...) do { if (getenv("SHIM_TRACE")) { fprintf(stderr, "[ev] " __VA_ARGS__); fputc(10, stderr); } } while (0)

namespace {

// A callback arriving from the event loop is not inside any JavaScript call, so
// nothing has set the env and there is no handle scope open. Without both, the
// first N-API call made from HP's code aborts the process with no message --
// which is what happened the moment a reply came back from the bus.
class LoopEntry {
public:
    LoopEntry() : fScope(nullptr), fEnv(v8::ModuleEnv()), fPrevious(v8::CurrentEnv())
    {
        if (!fEnv)
            return;
        v8::SetCurrentEnv(fEnv);
        napi_open_handle_scope(fEnv, &fScope);
    }
    ~LoopEntry()
    {
        if (fScope)
            napi_close_handle_scope(fEnv, fScope);
        v8::SetCurrentEnv(fPrevious);
    }
    bool ok() const { return fEnv != nullptr; }
private:
    napi_handle_scope fScope;
    napi_env fEnv;
    napi_env fPrevious;
};

// The loop node is running. libev's ev_default_loop() took no arguments, so
// there is nowhere to pass one in, and it is fetched from the env the shim is
// already tracking.
uv_loop_t* NodeLoop()
{
    static uv_loop_t* loop = nullptr;
    if (!loop) {
        napi_env env = v8::CurrentEnv();
        if (env)
            napi_get_uv_event_loop(env, &loop);
    }
    return loop;
}

// See ev_unref: libev counted per loop, libuv counts per handle.
uv_handle_t* gLastStarted = nullptr;

}  // namespace

extern "C" {

struct ev_loop* ev_default_loop_uv(void)
{
    return reinterpret_cast<struct ev_loop*>(NodeLoop());
}

static uv_loop_t* Loop(struct ev_loop* loop)
{
    return loop ? reinterpret_cast<uv_loop_t*>(loop) : NodeLoop();
}

// --- io ---------------------------------------------------------------------
//
// The uv_poll_t handles are owned here, one per descriptor, and not by the
// ev_io structs.
//
// node_ls2.cpp keeps its ev_io watchers in a malloc'd array that it frees and
// reallocates whenever GLib asks for more descriptors than last time. That was
// fine for libev, where a stopped watcher can be freed immediately. It is not
// fine for libuv: uv_close is asynchronous, so the handle's memory has to stay
// valid until the close callback runs, and reusing it -- ev_io_init memsets it
// and uv_poll_init takes it again -- corrupts libuv's internal lists. Keeping
// the handles in a table keyed by descriptor sidesteps the whole question.

namespace {

struct PollSlot {
    uv_poll_t poll;
    ev_io* owner;
    bool started;
    int events;
};

std::map<int, PollSlot*>& Polls()
{
    static std::map<int, PollSlot*> polls;
    return polls;
}

}  // namespace

static void PollBridge(uv_poll_t* handle, int status, int events)
{
    PollSlot* slot = static_cast<PollSlot*>(handle->data);
    if (!slot || !slot->owner)
        return;
    ev_io* w = slot->owner;

    if (status < 0) {
        // An error on the descriptor is reported to GLib as readable, as libev
        // did: the read then fails and GLib takes it from there.
        w->pending |= EV_READ;
    } else {
        if (events & UV_READABLE)
            w->pending |= EV_READ;
        if (events & UV_WRITABLE)
            w->pending |= EV_WRITE;
    }

    LoopEntry entry;
    if (entry.ok() && w->cb)
        w->cb(ev_default_loop_uv(), w, w->pending);
}

void ev_io_init(ev_io* w, ev_io_cb cb, int fd, int events)
{
    // Handed out of a malloc'd array, so nothing can be assumed about what was
    // in them.
    memset(w, 0, sizeof(*w));
    w->cb = cb;
    w->fd = fd;
    w->events = events;
}

void ev_io_start(struct ev_loop* loop, ev_io* w)
{
    uv_loop_t* l = Loop(loop);
    if (!l)
        return;

    PollSlot*& slot = Polls()[w->fd];
    if (!slot) {
        slot = new PollSlot();
        slot->owner = nullptr;
        slot->started = false;
        slot->events = 0;
        if (uv_poll_init(l, &slot->poll, w->fd) != 0) {
            delete slot;
            Polls().erase(w->fd);
            return;
        }
        slot->poll.data = slot;
    }

    slot->owner = w;
    int events = 0;
    if (w->events & EV_READ)
        events |= UV_READABLE;
    if (w->events & EV_WRITE)
        events |= UV_WRITABLE;

    if (!slot->started || slot->events != events) {
        if (uv_poll_start(&slot->poll, events, PollBridge) != 0)
            return;
        slot->started = true;
        slot->events = events;
    }
    w->active = 1;
    gLastStarted = reinterpret_cast<uv_handle_t*>(&slot->poll);
}

void ev_io_stop(struct ev_loop*, ev_io* w)
{
    w->active = 0;
    auto it = Polls().find(w->fd);
    if (it == Polls().end() || it->second->owner != w)
        return;
    // Only detached, not stopped: prepare re-arms the same descriptors on the
    // very next iteration, and stopping and restarting a poll every time costs
    // two syscalls per descriptor for nothing.
    it->second->owner = nullptr;
}

// --- prepare and check ------------------------------------------------------

static void PrepareBridge(uv_prepare_t* handle)
{
    LoopEntry entry;
    if (!entry.ok()) { TRACE("prepare: NO ENV"); return; }
    ev_prepare* w = static_cast<ev_prepare*>(handle->data);
    if (w && w->cb)
        w->cb(ev_default_loop_uv(), w, 0);
}

void ev_prepare_init(ev_prepare* w, ev_prepare_cb cb)
{
    memset(w, 0, sizeof(*w));
    w->cb = cb;
}

void ev_prepare_start(struct ev_loop* loop, ev_prepare* w)
{
    if (w->active)
        return;
    uv_loop_t* l = Loop(loop);
    if (!l || uv_prepare_init(l, &w->prepare) != 0)
        return;
    w->prepare.data = w;
    uv_prepare_start(&w->prepare, PrepareBridge);
    w->active = 1;
    gLastStarted = reinterpret_cast<uv_handle_t*>(&w->prepare);
}

void ev_prepare_stop(struct ev_loop*, ev_prepare* w)
{
    if (!w->active)
        return;
    uv_prepare_stop(&w->prepare);
    w->active = 0;
}

static void CheckBridge(uv_check_t* handle)
{
    LoopEntry entry;
    if (!entry.ok()) { TRACE("check: NO ENV"); return; }
    ev_check* w = static_cast<ev_check*>(handle->data);
    if (w && w->cb)
        w->cb(ev_default_loop_uv(), w, 0);
}

void ev_check_init(ev_check* w, ev_check_cb cb)
{
    memset(w, 0, sizeof(*w));
    w->cb = cb;
}

void ev_check_start(struct ev_loop* loop, ev_check* w)
{
    if (w->active)
        return;
    uv_loop_t* l = Loop(loop);
    if (!l || uv_check_init(l, &w->check) != 0)
        return;
    w->check.data = w;
    uv_check_start(&w->check, CheckBridge);
    w->active = 1;
    gLastStarted = reinterpret_cast<uv_handle_t*>(&w->check);
}

void ev_check_stop(struct ev_loop*, ev_check* w)
{
    if (!w->active)
        return;
    uv_check_stop(&w->check);
    w->active = 0;
}

// --- timer ------------------------------------------------------------------

static void TimerBridge(uv_timer_t* handle)
{
    LoopEntry entry;
    if (!entry.ok())
        return;
    ev_timer* w = static_cast<ev_timer*>(handle->data);
    if (w && w->cb)
        w->cb(ev_default_loop_uv(), w, 0);
}

void ev_timer_init_uv(ev_timer* w, ev_timer_cb cb)
{
    memset(w, 0, sizeof(*w));
    w->cb = cb;
}

void ev_timer_set(ev_timer* w, double after, double repeat)
{
    w->after = after;
    w->repeat = repeat;
}

void ev_timer_start(struct ev_loop* loop, ev_timer* w)
{
    uv_loop_t* l = Loop(loop);
    if (!l)
        return;
    if (!w->timer.data) {
        if (uv_timer_init(l, &w->timer) != 0)
            return;
        w->timer.data = w;
    }
    // libev counts in seconds, libuv in milliseconds.
    uint64_t after = static_cast<uint64_t>(w->after * 1000.0 + 0.5);
    uint64_t repeat = static_cast<uint64_t>(w->repeat * 1000.0 + 0.5);
    uv_timer_start(&w->timer, TimerBridge, after, repeat);
    w->active = 1;
    // The timeout GLib asks for must not by itself keep node alive.
    uv_unref(reinterpret_cast<uv_handle_t*>(&w->timer));
}

void ev_timer_stop(struct ev_loop*, ev_timer* w)
{
    if (!w->active)
        return;
    uv_timer_stop(&w->timer);
    w->active = 0;
}

// --- pending, active, refcounts ---------------------------------------------
//
// Every watcher struct here begins with data, cb, ... and keeps its active flag
// at a known place, but the layouts differ, so these three take a void* and are
// only correct for the types node_ls2.cpp passes them: ev_io for pending,
// ev_timer for active.

int ev_is_pending(const void* w)
{
    return static_cast<const ev_io*>(w)->pending != 0;
}

int ev_clear_pending(struct ev_loop*, void* w)
{
    ev_io* io = static_cast<ev_io*>(w);
    int revents = io->pending;
    if (revents) TRACE("clear_pending fd=%d revents=%d", io->fd, revents);
    io->pending = 0;
    return revents;
}

int ev_is_active(const void* w)
{
    return static_cast<const ev_timer*>(w)->active != 0;
}

void ev_unref(struct ev_loop*)
{
    if (gLastStarted && !uv_is_closing(gLastStarted))
        uv_unref(gLastStarted);
}

void ev_ref(struct ev_loop*)
{
    if (gLastStarted && !uv_is_closing(gLastStarted))
        uv_ref(gLastStarted);
}

}  // extern "C"
