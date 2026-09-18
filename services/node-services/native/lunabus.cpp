// lunabus.node: luna-service2 for node, written for kit/lunabus.ts.
//
// Replaces HP's palmbus (node 0.4 C++ on adapters/node-v8-shim) for the
// services in services/node-services. Plain Node-API, so it keeps loading on
// later node releases without a rebuild.
//
// The one hard part is the event loop. luna-service2 runs on a glib main
// context; node runs on libuv. The context here is driven from libuv's own
// loop, the way glib's documentation describes for foreign loops:
//
//   uv_prepare  -> g_main_context_prepare + query: one uv_poll per descriptor,
//                  and a uv_timer for glib's timeout
//   uv_poll     -> records what became ready
//   uv_check    -> g_main_context_check + dispatch
//
// None of those handles keeps node alive. What does is `alive`, referenced
// while at least one bus handle is open: a script that closes its handles
// ends, and a service, which never closes its own, keeps running.
//
// JavaScript API (one function per operation; the TypeScript side shapes it):
//
//   open(name | null, publicBus, onRequest(message), onCancel(message)) -> handle
//   call(handle, uri, payload, oneReply, onResponse(message)) -> token
//   cancel(handle, token)
//   registerMethod(handle, category, method)
//   subscriptionAdd(handle, key, message)
//   respond(message, payload) -> boolean
//   close(handle)
//
// A message reaches JavaScript as a plain object with its fields already read,
// plus `ref`, which keeps the LSMessage alive for respond and subscriptionAdd.

#include <node_api.h>
#include <uv.h>

#include <sys/stat.h>

#include <glib.h>
#include <lunaservice.h>

#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

// ---- errors ------------------------------------------------------------------

// Thrown inside this file, turned into a JavaScript exception at the boundary.
struct JsThrow {
    std::string message;
};

struct LsError {
    LSError error{};
    LsError() { LSErrorInit(&error); }
    ~LsError() { LSErrorFree(&error); }
    LsError(const LsError&) = delete;
    LsError& operator=(const LsError&) = delete;

    [[noreturn]] void raise(const char* what) const {
        throw JsThrow{std::string(what) + ": " + (error.message ? error.message : "unknown error")};
    }
};

void check(napi_env env, napi_status status) {
    if (status != napi_ok) {
        const napi_extended_error_info* info = nullptr;
        napi_get_last_error_info(env, &info);
        throw JsThrow{info && info->error_message ? info->error_message : "Node-API call failed"};
    }
}

// Runs `body`, turning JsThrow (and anything else) into a pending exception.
template <typename Body>
napi_value guarded(napi_env env, Body&& body) {
    try {
        return body();
    } catch (const JsThrow& error) {
        bool pending = false;
        napi_is_exception_pending(env, &pending);
        if (!pending) {
            napi_throw_error(env, nullptr, error.message.c_str());
        }
    } catch (const std::exception& error) {
        napi_throw_error(env, nullptr, error.what());
    }
    return nullptr;
}

// ---- values ------------------------------------------------------------------

napi_value undefinedValue(napi_env env) {
    napi_value value;
    check(env, napi_get_undefined(env, &value));
    return value;
}

napi_value stringValue(napi_env env, const char* text) {
    if (!text) {
        return undefinedValue(env);
    }
    napi_value value;
    check(env, napi_create_string_utf8(env, text, NAPI_AUTO_LENGTH, &value));
    return value;
}

napi_value boolValue(napi_env env, bool flag) {
    napi_value value;
    check(env, napi_get_boolean(env, flag, &value));
    return value;
}

std::string stringArg(napi_env env, napi_value value, const char* what) {
    size_t length = 0;
    if (napi_get_value_string_utf8(env, value, nullptr, 0, &length) != napi_ok) {
        throw JsThrow{std::string(what) + " must be a string"};
    }
    std::string text(length, '\0');
    check(env, napi_get_value_string_utf8(env, value, text.data(), length + 1, &length));
    return text;
}

bool boolArg(napi_env env, napi_value value, const char* what) {
    bool flag = false;
    if (napi_get_value_bool(env, value, &flag) != napi_ok) {
        throw JsThrow{std::string(what) + " must be a boolean"};
    }
    return flag;
}

std::vector<napi_value> args(napi_env env, napi_callback_info info, size_t count) {
    std::vector<napi_value> values(count);
    size_t given = count;
    check(env, napi_get_cb_info(env, info, &given, values.data(), nullptr, nullptr));
    if (given < count) {
        throw JsThrow{"expected " + std::to_string(count) + " arguments"};
    }
    return values;
}

// A persistent reference to a JavaScript function.
struct FunctionRef {
    napi_env env = nullptr;
    napi_ref ref = nullptr;

    FunctionRef() = default;
    FunctionRef(napi_env e, napi_value fn) : env(e) {
        napi_valuetype type;
        check(env, napi_typeof(env, fn, &type));
        if (type != napi_function) {
            throw JsThrow{"expected a function"};
        }
        check(env, napi_create_reference(env, fn, 1, &ref));
    }
    ~FunctionRef() {
        if (ref) {
            napi_delete_reference(env, ref);
        }
    }
    FunctionRef(const FunctionRef&) = delete;
    FunctionRef& operator=(const FunctionRef&) = delete;
};

// ---- the glib pump -----------------------------------------------------------

struct Watch {
    uv_poll_t poll{};
    int events = 0;      // uv events asked for
    gushort ready = 0;   // glib events seen since the last prepare
    // What the descriptor was when the poll started. luna-service2 may close
    // a socket and accept another under the same number within one dispatch;
    // epoll dropped the old one on close and libuv does not know, so a number
    // that now names another file needs a new poll.
    dev_t device = 0;
    ino_t inode = 0;
};

bool sameFile(const Watch& watch, const struct stat& now) {
    return watch.device == now.st_dev && watch.inode == now.st_ino;
}

struct Handle;

struct Pump {
    GMainContext* context = nullptr;
    GMainLoop* loop = nullptr;
    uv_prepare_t prepare{};
    uv_check_t check{};
    uv_timer_t timer{};
    uv_async_t alive{};
    std::vector<GPollFD> fds;
    std::map<int, Watch*> watches;
    gint priority = 0;
    int openHandles = 0;
    // Set while glib dispatches, which is when luna-service2 is inside its own
    // callbacks. JavaScript runs in there too (its microtasks drain at the end
    // of each callback), and a handle it closes cannot be unregistered until
    // luna-service2 is out again: it would free what the caller still uses.
    bool dispatching = false;
    std::vector<std::shared_ptr<Handle>> toUnregister;
};

Pump* thePump = nullptr;

int uvEventsFor(gushort events) {
    int uv = UV_DISCONNECT;
    if (events & G_IO_IN) {
        uv |= UV_READABLE;
    }
    if (events & G_IO_OUT) {
        uv |= UV_WRITABLE;
    }
    if (events & G_IO_PRI) {
        uv |= UV_PRIORITIZED;
    }
    return uv;
}

void closeWatch(Watch* watch) {
    uv_poll_stop(&watch->poll);
    uv_close(reinterpret_cast<uv_handle_t*>(&watch->poll),
             [](uv_handle_t* handle) { delete static_cast<Watch*>(handle->data); });
}

void onReady(uv_poll_t* handle, int status, int events) {
    auto* watch = static_cast<Watch*>(handle->data);
    if (status < 0) {
        watch->ready |= G_IO_ERR;
        return;
    }
    if (events & UV_READABLE) {
        watch->ready |= G_IO_IN;
    }
    if (events & UV_WRITABLE) {
        watch->ready |= G_IO_OUT;
    }
    if (events & UV_PRIORITIZED) {
        watch->ready |= G_IO_PRI;
    }
    if (events & UV_DISCONNECT) {
        watch->ready |= G_IO_HUP;
    }
}

void onPrepare(uv_prepare_t* handle) {
    auto* pump = static_cast<Pump*>(handle->data);
    g_main_context_prepare(pump->context, &pump->priority);

    gint timeout = -1;
    pump->fds.resize(std::max<size_t>(pump->fds.size(), 8));
    for (;;) {
        const gint needed = g_main_context_query(pump->context, pump->priority, &timeout,
                                                 pump->fds.data(), static_cast<gint>(pump->fds.size()));
        if (static_cast<size_t>(needed) <= pump->fds.size()) {
            pump->fds.resize(static_cast<size_t>(needed));
            break;
        }
        pump->fds.resize(static_cast<size_t>(needed));
    }

    // glib may list one descriptor twice; libuv takes one poll per descriptor.
    std::map<int, gushort> wanted;
    for (const GPollFD& fd : pump->fds) {
        wanted[fd.fd] |= fd.events;
    }
    for (auto it = pump->watches.begin(); it != pump->watches.end();) {
        if (!wanted.contains(it->first)) {
            closeWatch(it->second);
            it = pump->watches.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& [fd, events] : wanted) {
        const int uv = uvEventsFor(events);
        struct stat now {};
        auto found = pump->watches.find(fd);
        if (fstat(fd, &now) != 0) {
            if (found != pump->watches.end()) {
                closeWatch(found->second);
                pump->watches.erase(found);
            }
            continue;
        }
        Watch* watch = found == pump->watches.end() ? nullptr : found->second;
        if (watch && !sameFile(*watch, now)) {
            closeWatch(watch);
            pump->watches.erase(found);
            watch = nullptr;
        }
        if (!watch) {
            watch = new Watch;
            watch->poll.data = watch;
            watch->device = now.st_dev;
            watch->inode = now.st_ino;
            if (uv_poll_init(uv_default_loop(), &watch->poll, fd) != 0) {
                delete watch;
                continue;
            }
            uv_unref(reinterpret_cast<uv_handle_t*>(&watch->poll));
            pump->watches[fd] = watch;
        }
        watch->ready = 0;
        if (watch->events != uv) {
            watch->events = uv;
            uv_poll_start(&watch->poll, uv, onReady);
        }
    }

    if (timeout >= 0) {
        uv_timer_start(&pump->timer, [](uv_timer_t*) {}, static_cast<uint64_t>(timeout), 0);
    } else {
        uv_timer_stop(&pump->timer);
    }
}

void unregisterClosed(Pump* pump);

void onCheck(uv_check_t* handle) {
    auto* pump = static_cast<Pump*>(handle->data);
    for (GPollFD& fd : pump->fds) {
        auto found = pump->watches.find(fd.fd);
        fd.revents = found == pump->watches.end()
            ? 0 : static_cast<gushort>(found->second->ready & (fd.events | G_IO_HUP | G_IO_ERR));
    }
    const bool ready = g_main_context_check(pump->context, pump->priority, pump->fds.data(),
                                            static_cast<gint>(pump->fds.size()));
    if (ready) {
        pump->dispatching = true;
        g_main_context_dispatch(pump->context);
        pump->dispatching = false;
    }
    unregisterClosed(pump);
}

Pump* startPump() {
    auto* pump = new Pump;
    pump->context = g_main_context_new();
    g_main_context_acquire(pump->context);
    pump->loop = g_main_loop_new(pump->context, FALSE);
    uv_loop_t* uv = uv_default_loop();

    uv_prepare_init(uv, &pump->prepare);
    pump->prepare.data = pump;
    uv_prepare_start(&pump->prepare, onPrepare);
    uv_unref(reinterpret_cast<uv_handle_t*>(&pump->prepare));

    uv_check_init(uv, &pump->check);
    pump->check.data = pump;
    uv_check_start(&pump->check, onCheck);
    uv_unref(reinterpret_cast<uv_handle_t*>(&pump->check));

    uv_timer_init(uv, &pump->timer);
    uv_unref(reinterpret_cast<uv_handle_t*>(&pump->timer));

    uv_async_init(uv, &pump->alive, [](uv_async_t*) {});
    uv_unref(reinterpret_cast<uv_handle_t*>(&pump->alive));
    return pump;
}

void holdAlive(Pump* pump, int delta) {
    const bool was = pump->openHandles > 0;
    pump->openHandles += delta;
    const bool is = pump->openHandles > 0;
    if (is && !was) {
        uv_ref(reinterpret_cast<uv_handle_t*>(&pump->alive));
    } else if (was && !is) {
        uv_unref(reinterpret_cast<uv_handle_t*>(&pump->alive));
    }
}

// ---- handles, calls, messages -----------------------------------------------

struct Handle;

struct Call {
    std::weak_ptr<Handle> handle;
    std::unique_ptr<FunctionRef> onResponse;
    LSMessageToken token = LSMESSAGE_TOKEN_INVALID;
    bool oneReply = false;
    bool inCallback = false;
    bool finished = false;
};

struct Handle {
    napi_env env = nullptr;
    LSHandle* sh = nullptr;
    bool closed = false;
    FunctionRef onRequest;
    FunctionRef onCancel;
    napi_async_context async = nullptr;
    // LS2 keeps pointers into these, so they live as long as the handle.
    std::vector<std::unique_ptr<LSMethod[]>> tables;
    std::vector<std::unique_ptr<std::string>> names;
    std::map<LSMessageToken, std::unique_ptr<Call>> calls;
    std::weak_ptr<Handle> self;

    Handle(napi_env e, napi_value request, napi_value cancel) : env(e), onRequest(e, request), onCancel(e, cancel) {}
};

// What JavaScript holds: the handle, kept alive by shared ownership with the
// messages that still point into its connection.
struct HandleBox {
    std::shared_ptr<Handle> handle;
};

struct MessageBox {
    LSMessage* message;
    std::shared_ptr<Handle> handle;
};

// LSUnregister closes the handle's sockets, and the next handle opened may get
// the same descriptor numbers. A poll left on a closed descriptor never sees
// the new socket (epoll dropped it on close; libuv does not know), so every
// poll goes first and the next prepare starts them again for what is left.
void dropWatches(Pump* pump) {
    for (auto& [fd, watch] : pump->watches) {
        closeWatch(watch);
    }
    pump->watches.clear();
}

// The async context goes here, not in closeHandle: a handle closed from
// JavaScript inside its own callback is still inside napi_make_callback on
// that context.
void unregister(Handle& handle) {
    if (thePump) {
        dropWatches(thePump);
    }
    LsError error;
    LSUnregister(handle.sh, &error.error);
    handle.sh = nullptr;
    handle.calls.clear();
    if (handle.async) {
        napi_async_destroy(handle.env, handle.async);
        handle.async = nullptr;
    }
}

void unregisterClosed(Pump* pump) {
    auto closing = std::move(pump->toUnregister);
    pump->toUnregister.clear();
    for (auto& handle : closing) {
        unregister(*handle);
    }
}

// Closed at once for JavaScript: no more requests, replies or cancels reach
// it. Unregistered at once too, unless luna-service2 is dispatching.
void closeHandle(const std::shared_ptr<Handle>& handle) {
    if (handle->closed) {
        return;
    }
    handle->closed = true;
    for (auto& [token, call] : handle->calls) {
        call->finished = true;
    }
    if (thePump && thePump->dispatching) {
        thePump->toUnregister.push_back(handle);
    } else {
        unregister(*handle);
    }
    if (thePump) {
        holdAlive(thePump, -1);
    }
}

std::shared_ptr<Handle> handleArg(napi_env env, napi_value value) {
    void* data = nullptr;
    if (napi_get_value_external(env, value, &data) != napi_ok || !data) {
        throw JsThrow{"expected a bus handle"};
    }
    auto handle = static_cast<HandleBox*>(data)->handle;
    if (handle->closed) {
        throw JsThrow{"the bus handle is closed"};
    }
    return handle;
}

MessageBox* messageArg(napi_env env, napi_value object) {
    napi_value ref;
    check(env, napi_get_named_property(env, object, "ref", &ref));
    void* data = nullptr;
    if (napi_get_value_external(env, ref, &data) != napi_ok || !data) {
        throw JsThrow{"expected a bus message"};
    }
    return static_cast<MessageBox*>(data);
}

// Which fields a message has depends on what it is. luna-service2 asserts, or
// crashes outright, when asked for a sender or an application id a message does
// not carry: a reply the hub generated has no sender at all. So each kind reads
// only its own fields; the rest stay undefined.
enum class Kind { Request, Response, Cancel };

napi_value messageObject(napi_env env, const std::shared_ptr<Handle>& handle, LSMessage* message, Kind kind) {
    napi_value object;
    check(env, napi_create_object(env, &object));
    const auto set = [&](const char* key, napi_value value) {
        check(env, napi_set_named_property(env, object, key, value));
    };
    set("payload", stringValue(env, LSMessageGetPayload(message)));
    set("category", stringValue(env, LSMessageGetCategory(message)));
    if (kind == Kind::Request) {
        set("method", stringValue(env, LSMessageGetMethod(message)));
        set("sender", stringValue(env, LSMessageGetSender(message)));
        set("senderServiceName", stringValue(env, LSMessageGetSenderServiceName(message)));
        set("applicationId", stringValue(env, LSMessageGetApplicationID(message)));
        set("isSubscription", boolValue(env, LSMessageIsSubscription(message)));
    } else {
        set("isSubscription", boolValue(env, false));
    }
    if (kind != Kind::Response) {
        set("uniqueToken", stringValue(env, LSMessageGetUniqueToken(message)));
    }

    LSMessageRef(message);
    auto* box = new MessageBox{message, handle};
    napi_value ref;
    const napi_status status = napi_create_external(env, box, [](napi_env, void* data, void*) {
        auto* box = static_cast<MessageBox*>(data);
        LSMessageUnref(box->message);
        delete box;
    }, nullptr, &ref);
    if (status != napi_ok) {
        LSMessageUnref(message);
        delete box;
        check(env, status);
    }
    set("ref", ref);
    return object;
}

// Calls a JavaScript function from a glib callback. A throw inside it is
// reported the way node reports any uncaught exception.
void invoke(Handle& handle, const FunctionRef& function, LSMessage* message, Kind kind) {
    napi_env env = handle.env;
    napi_handle_scope scope;
    if (napi_open_handle_scope(env, &scope) != napi_ok) {
        return;
    }
    try {
        auto shared = handle.self.lock();
        napi_value fn;
        check(env, napi_get_reference_value(env, function.ref, &fn));
        napi_value argv[] = {messageObject(env, shared, message, kind)};
        napi_value result;
        napi_value global;
        check(env, napi_get_global(env, &global));
        const napi_status status = napi_make_callback(env, handle.async, global, fn, 1, argv, &result);
        if (status == napi_pending_exception) {
            napi_value exception;
            napi_get_and_clear_last_exception(env, &exception);
            napi_fatal_exception(env, exception);
        }
    } catch (const JsThrow& error) {
        g_warning("lunabus: %s", error.message.c_str());
    }
    napi_close_handle_scope(env, scope);
}

bool onMethod(LSHandle*, LSMessage* message, void* context) {
    auto* handle = static_cast<Handle*>(context);
    if (!handle->closed) {
        invoke(*handle, handle->onRequest, message, Kind::Request);
    }
    return true;
}

bool onSubscriptionCancel(LSHandle*, LSMessage* message, void* context) {
    auto* handle = static_cast<Handle*>(context);
    if (!handle->closed) {
        invoke(*handle, handle->onCancel, message, Kind::Cancel);
    }
    return true;
}

bool onResponse(LSHandle*, LSMessage* message, void* context) {
    auto* call = static_cast<Call*>(context);
    auto handle = call->handle.lock();
    if (!handle || handle->closed || call->finished) {
        return true;
    }
    call->inCallback = true;
    invoke(*handle, *call->onResponse, message, Kind::Response);
    call->inCallback = false;
    if (handle->closed) {
        return true;
    }
    // luna-service2 forgets a one-reply call after its reply. Any other call
    // lasts until JavaScript cancels it, which kit/luna.ts does on a failure.
    if (call->oneReply) {
        call->finished = true;
    }
    if (call->finished) {
        handle->calls.erase(call->token);
    }
    return true;
}

// ---- the exported functions --------------------------------------------------

napi_value open(napi_env env, napi_callback_info info) {
    return guarded(env, [&] {
        auto argv = args(env, info, 4);
        napi_valuetype type;
        check(env, napi_typeof(env, argv[0], &type));
        const std::string name = type == napi_null ? std::string() : stringArg(env, argv[0], "name");
        const bool publicBus = boolArg(env, argv[1], "publicBus");

        auto handle = std::make_shared<Handle>(env, argv[2], argv[3]);
        handle->self = handle;
        napi_value resourceName = stringValue(env, "lunabus");
        check(env, napi_async_init(env, nullptr, resourceName, &handle->async));
        LsError error;
        if (!LSRegisterPubPriv(type == napi_null ? nullptr : name.c_str(), &handle->sh, publicBus, &error.error)) {
            napi_async_destroy(env, handle->async);
            handle->async = nullptr;
            error.raise("LSRegister");
        }
        // From here on the handle is registered: any failure closes it again,
        // so luna-service2 keeps no pointer to a Handle that is about to go.
        holdAlive(thePump, +1);
        try {
            if (!LSGmainAttach(handle->sh, thePump->loop, &error.error)) {
                error.raise("LSGmainAttach");
            }
            if (!LSSubscriptionSetCancelFunction(handle->sh, onSubscriptionCancel, handle.get(), &error.error)) {
                error.raise("LSSubscriptionSetCancelFunction");
            }
            napi_value external;
            auto box = std::make_unique<HandleBox>(HandleBox{handle});
            check(env, napi_create_external(env, box.get(), [](napi_env, void* data, void*) {
                auto* owned = static_cast<HandleBox*>(data);
                closeHandle(owned->handle);
                delete owned;
            }, nullptr, &external));
            box.release();
            return external;
        } catch (...) {
            closeHandle(handle);
            throw;
        }
    });
}

napi_value call(napi_env env, napi_callback_info info) {
    return guarded(env, [&] {
        auto argv = args(env, info, 5);
        auto handle = handleArg(env, argv[0]);
        const std::string uri = stringArg(env, argv[1], "uri");
        const std::string payload = stringArg(env, argv[2], "payload");
        auto pending = std::make_unique<Call>();
        pending->handle = handle;
        pending->oneReply = boolArg(env, argv[3], "oneReply");
        pending->onResponse = std::make_unique<FunctionRef>(env, argv[4]);

        LsError error;
        const bool sent = pending->oneReply
            ? LSCallOneReply(handle->sh, uri.c_str(), payload.c_str(), onResponse, pending.get(),
                             &pending->token, &error.error)
            : LSCall(handle->sh, uri.c_str(), payload.c_str(), onResponse, pending.get(),
                     &pending->token, &error.error);
        if (!sent) {
            error.raise("LSCall");
        }
        const LSMessageToken token = pending->token;
        handle->calls[token] = std::move(pending);
        napi_value result;
        check(env, napi_create_double(env, static_cast<double>(token), &result));
        return result;
    });
}

napi_value cancel(napi_env env, napi_callback_info info) {
    return guarded(env, [&] {
        auto argv = args(env, info, 2);
        auto handle = handleArg(env, argv[0]);
        double value = 0;
        check(env, napi_get_value_double(env, argv[1], &value));
        const auto token = static_cast<LSMessageToken>(value);
        auto found = handle->calls.find(token);
        if (found == handle->calls.end()) {
            return undefinedValue(env);
        }
        Call* pending = found->second.get();
        if (!pending->finished) {
            pending->finished = true;
            LsError error;
            LSCallCancel(handle->sh, token, &error.error);
        }
        // Inside its own response the call is erased when the callback returns.
        if (!pending->inCallback) {
            handle->calls.erase(found);
        }
        return undefinedValue(env);
    });
}

napi_value registerMethod(napi_env env, napi_callback_info info) {
    return guarded(env, [&] {
        auto argv = args(env, info, 3);
        auto handle = handleArg(env, argv[0]);
        // The strings themselves do not move when the vector grows; the
        // unique_ptrs holding them do, so keep the string pointers, not
        // references to the elements.
        const std::string* category =
            handle->names.emplace_back(std::make_unique<std::string>(stringArg(env, argv[1], "category"))).get();
        const std::string* method =
            handle->names.emplace_back(std::make_unique<std::string>(stringArg(env, argv[2], "method"))).get();
        LSMethod* table = handle->tables.emplace_back(new LSMethod[2]).get();
        table[0] = LSMethod{method->c_str(), onMethod, static_cast<LSMethodFlags>(0)};
        table[1] = LSMethod{nullptr, nullptr, static_cast<LSMethodFlags>(0)};
        LsError error;
        if (!LSRegisterCategoryAppend(handle->sh, category->c_str(), table, nullptr, &error.error)) {
            error.raise("LSRegisterCategoryAppend");
        }
        if (!LSCategorySetData(handle->sh, category->c_str(), handle.get(), &error.error)) {
            error.raise("LSCategorySetData");
        }
        return undefinedValue(env);
    });
}

napi_value subscriptionAdd(napi_env env, napi_callback_info info) {
    return guarded(env, [&] {
        auto argv = args(env, info, 3);
        auto handle = handleArg(env, argv[0]);
        const std::string key = stringArg(env, argv[1], "key");
        MessageBox* box = messageArg(env, argv[2]);
        LsError error;
        if (!LSSubscriptionAdd(handle->sh, key.c_str(), box->message, &error.error)) {
            error.raise("LSSubscriptionAdd");
        }
        return undefinedValue(env);
    });
}

napi_value respond(napi_env env, napi_callback_info info) {
    return guarded(env, [&] {
        auto argv = args(env, info, 2);
        MessageBox* box = messageArg(env, argv[0]);
        const std::string payload = stringArg(env, argv[1], "payload");
        if (box->handle->closed) {
            return boolValue(env, false);
        }
        LsError error;
        return boolValue(env, LSMessageReply(box->handle->sh, box->message, payload.c_str(), &error.error));
    });
}

napi_value close(napi_env env, napi_callback_info info) {
    return guarded(env, [&] {
        auto argv = args(env, info, 1);
        void* data = nullptr;
        if (napi_get_value_external(env, argv[0], &data) != napi_ok || !data) {
            throw JsThrow{"expected a bus handle"};
        }
        closeHandle(static_cast<HandleBox*>(data)->handle);
        return undefinedValue(env);
    });
}

napi_value init(napi_env env, napi_value exports) {
    return guarded(env, [&] {
        // One glib pump, on node's main loop. A worker thread has a loop of its
        // own, and pumping its handles from the main thread would call into its
        // JavaScript from the wrong thread.
        uv_loop_t* loop = nullptr;
        check(env, napi_get_uv_event_loop(env, &loop));
        if (loop != uv_default_loop()) {
            throw JsThrow{"lunabus.node runs on the main thread only"};
        }
        if (!thePump) {
            thePump = startPump();
        }
        const std::pair<const char*, napi_callback> functions[] = {
            {"open", open}, {"call", call}, {"cancel", cancel}, {"registerMethod", registerMethod},
            {"subscriptionAdd", subscriptionAdd}, {"respond", respond}, {"close", close},
        };
        for (const auto& [name, fn] : functions) {
            napi_value value;
            check(env, napi_create_function(env, name, NAPI_AUTO_LENGTH, fn, nullptr, &value));
            check(env, napi_set_named_property(env, exports, name, value));
        }
        return exports;
    });
}

}  // namespace

NAPI_MODULE(NODE_GYP_MODULE_NAME, init)
