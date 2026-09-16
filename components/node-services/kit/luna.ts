// The bus, for services written in modern TypeScript.
//
//   const bus = openBus("com.example.service");
//
//   bus.method("add", ({ payload }) => ({ sum: payload.a + payload.b }));
//
//   // A subscription is an async generator: each yield is one reply. When the
//   // subscriber goes away, request.signal is aborted; pass it on to whatever
//   // the generator waits on, so the generator ends and its finally block runs.
//   bus.method("watch", async function* ({ signal }) {
//       for await (const change of bus.subscribe(uri, payload, { signal })) yield change;
//   });
//
//   const reply = await bus.call("luna://com.palm.db/find", { query });
//
// Replies follow the webOS convention every HP service used: `returnValue` is
// added when the handler leaves it out, and a failure is
// `{returnValue: false, errorCode, errorText}`. On the calling side a reply
// with `returnValue: false` becomes a LunaError.
//
// createBus takes what the bus depends on and returns the function that opens
// one; openBus is that function wired to the real addon and timers.

import { openHandle, type OpenHandle, type PalmHandle, type PalmMessage } from "./palmbus.ts";

export type Payload = Record<string, unknown>;

// The hub answers in this category when a call cannot be delivered at all:
// the service does not exist, or the caller is not allowed to reach it. Every
// such reply also carries returnValue false (luna-service2's callmap.c), which
// is what readReply goes by; palmbus ends the call on it.
export const HUB_ERROR_CATEGORY = "/com/palm/luna/private/error";

// ---- errors -----------------------------------------------------------------

export interface LunaError extends Error {
    readonly name: "LunaError";
    readonly errorCode: number | string;
    readonly errorText: string;
    // The whole failed reply, including any fields beyond code and text.
    readonly reply: Payload;
}

export const lunaError = (errorCode: number | string, errorText: string, reply: Payload = {}): LunaError =>
    Object.assign(new Error(errorText), { name: "LunaError" as const, errorCode, errorText, reply });

export const isLunaError = (value: unknown): value is LunaError =>
    value instanceof Error && value.name === "LunaError" && "errorCode" in value;

// ---- the API ----------------------------------------------------------------

export interface Request<P extends Payload = Payload> {
    readonly method: string;
    readonly category: string;
    readonly payload: P;
    // True when the caller asked for updates ("subscribe": true).
    readonly subscribe: boolean;
    readonly sender: string | undefined;
    readonly senderServiceName: string | undefined;
    readonly applicationId: string | undefined;
    // Aborted when a subscriber cancels, or when the bus closes. An async
    // generator cannot be stopped while it is waiting on something, only at a
    // yield, so this is how a waiting handler learns it should end.
    readonly signal: AbortSignal;
}

type Reply = Payload | void;

export type Handler<P extends Payload = Payload> =
    (request: Request<P>) => Reply | Promise<Reply> | AsyncIterable<Payload>;

export interface MethodOptions {
    // "/" when left out. HP's services register most methods there.
    readonly category?: string;
    // Seconds before the caller gets a 504 instead of waiting on. Only the
    // first reply is timed.
    readonly timeout?: number;
}

export interface CallOptions {
    // Seconds to wait for the reply before failing with a LunaError.
    readonly timeout?: number;
}

export interface SubscribeOptions {
    // Aborting it cancels the call and ends the iteration, even while the
    // loop is waiting for the next reply.
    readonly signal?: AbortSignal;
}

export interface BusOptions {
    // Register on the public bus instead of the private one. A service that
    // answers on both opens one bus for each, as HP's did.
    readonly public?: boolean;
}

export interface Bus {
    call<R extends Payload = Payload>(uri: string, payload?: Payload, options?: CallOptions): Promise<R>;
    // Every reply to a subscription, in order. Leaving the loop (break, return
    // or an exception) cancels the call. A failed reply is thrown and ends it.
    subscribe<R extends Payload = Payload>(uri: string, payload?: Payload,
                                           options?: SubscribeOptions): AsyncIterableIterator<R>;
    method<P extends Payload = Payload>(name: string, handler: Handler<P>, options?: MethodOptions): void;
    // Calls `onIdle` once nothing has been asked for `ms` milliseconds and no
    // request or subscription is still open. HP's services quit this way, a
    // few seconds after their last command, and the hub starts them again on
    // demand. Without `onIdle` the process exits.
    exitWhenIdle(ms: number, onIdle?: () => void): void;
    close(): void;
}

export type Timer = unknown;

export interface BusDeps {
    readonly openHandle: OpenHandle;
    readonly setTimer: (callback: () => void, ms: number) => Timer;
    readonly clearTimer: (timer: Timer) => void;
    readonly exit: () => void;
}

// ---- pure helpers -----------------------------------------------------------

const parse = (text: string): Payload => {
    try {
        const value: unknown = JSON.parse(text);
        if (value !== null && typeof value === "object" && !Array.isArray(value)) {
            return value as Payload;
        }
    } catch {
        // Falls through to the error below.
    }
    throw lunaError(-1, `Malformed JSON: ${text.slice(0, 80)}`);
};

// The reply a caller receives, or the LunaError it stands for.
export const readReply = (message: PalmMessage): Payload => {
    const reply = parse(message.payload());
    if (reply.returnValue !== false) {
        return reply;
    }
    const code = typeof reply.errorCode === "number" || typeof reply.errorCode === "string"
        ? reply.errorCode : -1;
    throw lunaError(code, typeof reply.errorText === "string" ? reply.errorText : "Call failed", reply);
};

export const successReply = (reply: Reply): Payload =>
    ({ ...(reply ?? {}), returnValue: reply?.returnValue ?? true });

export const errorReply = (error: unknown): Payload =>
    isLunaError(error)
        ? { ...error.reply, returnValue: false, errorCode: error.errorCode, errorText: error.errorText }
        : { returnValue: false, errorCode: -1, errorText: error instanceof Error ? error.message : String(error) };

const isAsyncIterable = (value: unknown): value is AsyncIterable<Payload> =>
    value !== null && typeof value === "object" && Symbol.asyncIterator in value;

const methodKey = (category: string, method: string): string =>
    `${category.endsWith("/") ? category : `${category}/`}${method}`;

// ---- calling ----------------------------------------------------------------

const callWith = (deps: BusDeps, handle: PalmHandle) =>
    <R extends Payload>(uri: string, payload: Payload = {}, options: CallOptions = {}): Promise<R> =>
        new Promise<R>((resolve, reject) => {
            const call = handle.call(uri, JSON.stringify(payload));
            const timer = options.timeout === undefined ? undefined : deps.setTimer(() => {
                call.cancel();
                reject(lunaError(-1, `Timed out after ${options.timeout} s calling ${uri}`));
            }, options.timeout * 1000);
            call.addListener("response", (message) => {
                deps.clearTimer(timer);
                try {
                    resolve(readReply(message) as R);
                } catch (error) {
                    reject(error);
                }
            });
        });

const subscribeWith = (handle: PalmHandle) =>
    <R extends Payload>(uri: string, payload: Payload = {}, options: SubscribeOptions = {}): AsyncIterableIterator<R> => {
        const queue: R[] = [];
        const state: { failure?: unknown; done: boolean; wake?: (() => void) | undefined } = { done: false };
        const { signal } = options;

        const wake = () => {
            const pending = state.wake;
            state.wake = undefined;
            pending?.();
        };

        const start = () => {
            if (signal?.aborted) {
                state.done = true;
                return undefined;
            }
            const call = handle.subscribe(uri, JSON.stringify({ ...payload, subscribe: true }));
            call.addListener("response", (message) => {
                if (state.done) {
                    return;
                }
                try {
                    queue.push(readReply(message) as R);
                } catch (error) {
                    state.failure = error;
                }
                wake();
            });
            return call;
        };
        const call = start();

        const finish = () => {
            if (!state.done) {
                state.done = true;
                call?.cancel();
            }
            wake();
        };
        signal?.addEventListener("abort", finish, { once: true });

        const next = async (): Promise<IteratorResult<R>> => {
            while (queue.length === 0 && state.failure === undefined && !state.done) {
                await new Promise<void>((resolve) => { state.wake = resolve; });
            }
            const value = queue.shift();
            if (value !== undefined) {
                return { value, done: false };
            }
            if (state.failure !== undefined) {
                const error = state.failure;
                state.failure = undefined;
                finish();
                throw error;
            }
            return { value: undefined, done: true };
        };

        const iterator: AsyncIterableIterator<R> = {
            [Symbol.asyncIterator]: () => iterator,
            next,
            return: async () => {
                queue.length = 0;
                finish();
                return { value: undefined, done: true };
            },
        };
        return iterator;
    };

// ---- answering --------------------------------------------------------------

interface Registered {
    readonly handler: Handler;
    readonly options: MethodOptions;
}

interface Live {
    readonly iterator: AsyncIterator<Payload>;
    readonly abort: AbortController;
}

interface Idle {
    readonly ms: number;
    readonly onIdle: () => void;
    timer: Timer;
}

export const createBus = (deps: BusDeps) => (name: string | null, options: BusOptions = {}): Bus => {
    const handle = deps.openHandle(name, options.public ?? false);
    const handlers = new Map<string, Registered>();
    // Open subscriptions, by the message token the hub reports on cancel.
    const live = new Map<string, Live>();
    const state: { busy: number; closed: boolean; idle: Idle | undefined } =
        { busy: 0, closed: false, idle: undefined };

    const armIdle = () => {
        const { idle } = state;
        if (!idle) {
            return;
        }
        deps.clearTimer(idle.timer);
        idle.timer = state.busy === 0 && live.size === 0 ? deps.setTimer(idle.onIdle, idle.ms) : undefined;
    };

    const setBusy = (delta: number) => {
        state.busy += delta;
        armIdle();
    };

    const respond = (message: PalmMessage, reply: Payload) => {
        if (!state.closed) {
            message.respond(JSON.stringify(reply));
        }
    };

    const endLive = (token: string): boolean => {
        const entry = live.get(token);
        if (!entry) {
            return false;
        }
        live.delete(token);
        entry.abort.abort();
        void entry.iterator.return?.();
        return true;
    };

    // Replies from a generator: the first one answers the request; while
    // subscribed, every later one is pushed until the generator ends or the
    // subscriber goes away.
    const stream = async (message: PalmMessage, request: Request, replies: AsyncIterable<Payload>,
                          abort: AbortController, answer: (reply: Payload) => void) => {
        const iterator = replies[Symbol.asyncIterator]();
        const token = message.uniqueToken();
        if (request.subscribe) {
            live.set(token, { iterator, abort });
            handle.subscriptionAdd(token, message);
        }
        const first = await iterator.next().catch((error: unknown) => {
            live.delete(token);
            throw error;
        });
        answer(successReply(first.done ? undefined : first.value));
        if (!request.subscribe || first.done) {
            live.delete(token);
            await iterator.return?.();
            return;
        }
        // An open subscription does not count as busy: it may last forever.
        setBusy(-1);
        try {
            for (let next = await iterator.next(); !next.done && live.has(token); next = await iterator.next()) {
                respond(message, successReply(next.value));
            }
        } catch (error) {
            respond(message, errorReply(error));
        } finally {
            state.busy += 1;
            endLive(token);
        }
    };

    const dispatch = async (message: PalmMessage) => {
        const registered = handlers.get(methodKey(message.category(), message.method()));
        if (!registered) {
            return;
        }
        setBusy(+1);
        const abort = new AbortController();
        const answered: { done: boolean; timer: Timer } = { done: false, timer: undefined };
        const answer = (reply: Payload) => {
            if (!answered.done) {
                answered.done = true;
                deps.clearTimer(answered.timer);
                respond(message, reply);
            }
        };
        const { timeout } = registered.options;
        if (timeout !== undefined) {
            answered.timer = deps.setTimer(() => answer({
                returnValue: false,
                errorCode: 504,
                errorText: `Timed out after ${timeout} s in ${message.method()}`,
            }), timeout * 1000);
        }
        try {
            const payload = parse(message.payload());
            const request: Request = {
                method: message.method(),
                category: message.category(),
                payload,
                subscribe: payload.subscribe === true,
                sender: message.sender(),
                senderServiceName: message.senderServiceName(),
                applicationId: message.applicationID(),
                signal: abort.signal,
            };
            const result = registered.handler(request);
            if (isAsyncIterable(result)) {
                await stream(message, request, result, abort, answer);
            } else {
                answer(successReply(await result));
            }
        } catch (error) {
            answer(errorReply(error));
        } finally {
            deps.clearTimer(answered.timer);
            setBusy(-1);
        }
    };

    handle.addListener("request", (message) => void dispatch(message));
    handle.addListener("cancel", (message) => {
        if (endLive(message.uniqueToken())) {
            armIdle();
        }
    });

    return {
        call: callWith(deps, handle),
        subscribe: subscribeWith(handle),
        method: (method, handler, methodOptions = {}) => {
            const category = methodOptions.category ?? "/";
            handle.registerMethod(category, method);
            handlers.set(methodKey(category, method), { handler: handler as Handler, options: methodOptions });
        },
        exitWhenIdle: (ms, onIdle = deps.exit) => {
            state.idle = { ms, onIdle, timer: undefined };
            armIdle();
        },
        close: () => {
            if (state.closed) {
                return;
            }
            state.closed = true;
            deps.clearTimer(state.idle?.timer);
            state.idle = undefined;
            for (const token of [...live.keys()]) {
                endLive(token);
            }
            handle.unregister();
        },
    };
};

export const openBus = createBus({
    openHandle,
    setTimer: (callback, ms) => setTimeout(callback, ms),
    clearTimer: (timer) => clearTimeout(timer as ReturnType<typeof setTimeout> | undefined),
    exit: () => process.exit(0),
});
