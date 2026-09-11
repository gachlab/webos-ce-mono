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

// The part of libev that node_ls2.cpp uses, on libuv.
//
// node 0.4's event loop was libev. node_ls2.cpp runs a GLib main context inside
// it, the standard way: a prepare watcher asks GLib which file descriptors it
// wants polled and arms one io watcher per descriptor, and a check watcher
// collects what fired and calls g_main_context_check and _dispatch. That is how
// LS2 -- which is GLib-based -- gets serviced by node's loop.
//
// Modern node uses libuv, which has the same three phases: uv_prepare_t,
// uv_poll_t, uv_check_t. So rather than rewrite the file, this gives it the
// libev names it calls, backed by libuv handles.
//
// The one place the two genuinely differ is pending events. libev marks a
// watcher pending during poll and lets you read and clear that in the check
// phase, which is what node_ls2.cpp does; libuv instead invokes the poll
// callback directly. So ev_io here records what its uv_poll callback saw, and
// ev_is_pending / ev_clear_pending report and reset that record. The behaviour
// node_ls2.cpp depends on is preserved; the mechanism underneath is not the
// same.

#ifndef WEBOS_EV_SHIM_H
#define WEBOS_EV_SHIM_H

#include <uv.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EV_READ   1
#define EV_WRITE  2

#define EV_MINPRI (-2)
#define EV_MAXPRI 2

struct ev_loop;

// libev with EV_MULTIPLICITY passes the loop as the first argument. node 0.4
// built it that way, so the callbacks in node_ls2.cpp are declared with EV_P_.
#define EV_P  struct ev_loop *loop
#define EV_P_ struct ev_loop *loop,
#define EV_A  loop
#define EV_A_ loop,

struct ev_loop *ev_default_loop_uv(void);
#define EV_DEFAULT     ev_default_loop_uv()
#define EV_DEFAULT_    ev_default_loop_uv(),
#define EV_DEFAULT_UC  ev_default_loop_uv()
#define EV_DEFAULT_UC_ ev_default_loop_uv(),

typedef struct ev_io ev_io;
typedef struct ev_prepare ev_prepare;
typedef struct ev_check ev_check;
typedef struct ev_timer ev_timer;

typedef void (*ev_io_cb)(struct ev_loop *loop, ev_io *w, int revents);
typedef void (*ev_prepare_cb)(struct ev_loop *loop, ev_prepare *w, int revents);
typedef void (*ev_check_cb)(struct ev_loop *loop, ev_check *w, int revents);
typedef void (*ev_timer_cb)(struct ev_loop *loop, ev_timer *w, int revents);

// node_ls2.cpp allocates arrays of these with malloc and recovers the enclosing
// struct with offsetof, so they have to stay plain C structs with `data` where
// libev put it.
struct ev_io {
    void *data;
    ev_io_cb cb;
    int fd;
    int events;
    int pending;        // what the uv_poll callback saw, not yet consumed
    int active;
    uv_poll_t poll;
};

struct ev_prepare {
    void *data;
    ev_prepare_cb cb;
    int active;
    uv_prepare_t prepare;
};

struct ev_check {
    void *data;
    ev_check_cb cb;
    int active;
    uv_check_t check;
};

struct ev_timer {
    void *data;
    ev_timer_cb cb;
    double after;
    double repeat;
    int active;
    uv_timer_t timer;
};

void ev_io_init(ev_io *w, ev_io_cb cb, int fd, int events);
void ev_io_start(struct ev_loop *loop, ev_io *w);
void ev_io_stop(struct ev_loop *loop, ev_io *w);

void ev_prepare_init(ev_prepare *w, ev_prepare_cb cb);
void ev_prepare_start(struct ev_loop *loop, ev_prepare *w);
void ev_prepare_stop(struct ev_loop *loop, ev_prepare *w);

void ev_check_init(ev_check *w, ev_check_cb cb);
void ev_check_start(struct ev_loop *loop, ev_check *w);
void ev_check_stop(struct ev_loop *loop, ev_check *w);

void ev_timer_init_uv(ev_timer *w, ev_timer_cb cb);
void ev_timer_set(ev_timer *w, double after, double repeat);
void ev_timer_start(struct ev_loop *loop, ev_timer *w);
void ev_timer_stop(struct ev_loop *loop, ev_timer *w);

// libev's ev_init is a macro over the watcher type. Only the timer uses it here.
#define ev_init(w, cb) ev_timer_init_uv((w), (cb))

int ev_is_pending(const void *w);
int ev_clear_pending(struct ev_loop *loop, void *w);
int ev_is_active(const void *w);

// Priorities order libev's callbacks within a phase. libuv has no equivalent and
// runs its phases in a fixed order -- prepare, poll, check -- which is the order
// this code needs anyway. Accepted and ignored.
#define ev_set_priority(w, pri) ((void)(pri))

// libev counted watchers to decide whether the loop still had work. In libuv
// that is per handle, so this unrefs the last handle started. node_ls2.cpp calls
// it once, immediately after starting the prepare watcher, which is exactly what
// it means to say.
void ev_unref(struct ev_loop *loop);
void ev_ref(struct ev_loop *loop);

#ifdef __cplusplus
}
#endif

#endif /* WEBOS_EV_SHIM_H */
