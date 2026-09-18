// The bus through WebAppMgr's PalmServiceBridge, which is what a card has.
//
// The bridge is one call: `new PalmServiceBridge()`, `call(uri, json)`, replies
// arrive as JSON text on `onservicecallback`, and `cancel()` ends it. One
// bridge carries one call at a time, so there is one per call in flight -- and
// a finished one is kept and handed to the next call rather than dropped:
// WebAppMgr parents each bridge to the page (PalmServiceBridgeFactory), so a
// card that polls would leave one native object and one signal connection
// behind every few seconds, for as long as it is open.

import {
    DEFAULT_TIMEOUT_MS, LunaCallError, LunaTimeout,
    failed, type LunaCallOptions, type LunaService, type LunaSubscribeOptions,
    type Payload, type Subscription,
} from "./service.ts";
import { systemTimers, type Timers } from "../../helpers/timers.ts";

// What WebAppMgr puts on the page.
export interface PalmServiceBridge {
    onservicecallback: ((reply: string) => void) | null;
    call(uri: string, payload: string): void;
    cancel(): void;
}

export interface BridgeDeps {
    // `new PalmServiceBridge()`, taken as a function so a test can hand over
    // its own and a browser can be told there is none.
    readonly open: () => PalmServiceBridge;
    readonly timers?: Timers;
    readonly log?: (message: string) => void;
}

const parse = (text: string): Payload => {
    try {
        const value: unknown = JSON.parse(text);
        return value !== null && typeof value === "object" ? value as Payload : {};
    } catch {
        return {};
    }
};

// The bridge wants a trailing method on the uri and no double slash; the
// services are written both ways in HP's code.
const asBridgeUri = (uri: string): string => uri.replace(/^luna:/, "palm:");

export const createBridgeLuna = (deps: BridgeDeps): LunaService => {
    const timers = deps.timers ?? systemTimers;
    // Bridges whose call is over, waiting to carry the next one.
    const free: PalmServiceBridge[] = [];
    const take = (): PalmServiceBridge => free.pop() ?? deps.open();
    const give = (bridge: PalmServiceBridge) => {
        bridge.onservicecallback = null;
        bridge.cancel();
        if (free.length < 4) {
            free.push(bridge);
        }
    };
    return {
        call: <Reply extends Payload>(uri: string, payload: Payload = {}, options: LunaCallOptions = {}) =>
            new Promise<Reply>((resolve, reject) => {
                const ms = options.timeoutMs ?? DEFAULT_TIMEOUT_MS;
                const bridge = take();
                let done = false;
                const finish = () => {
                    done = true;
                    timers.clearTimeout(handle);
                    give(bridge);
                };
                const handle = timers.setTimeout(() => {
                    if (!done) {
                        finish();
                        reject(new LunaTimeout(uri, ms));
                    }
                }, ms);
                bridge.onservicecallback = (text) => {
                    if (done) {
                        return;
                    }
                    const reply = parse(text);
                    finish();
                    if (failed(reply)) {
                        reject(new LunaCallError(uri, reply));
                    } else {
                        resolve(reply as Reply);
                    }
                };
                try {
                    bridge.call(asBridgeUri(uri), JSON.stringify(payload));
                } catch (error) {
                    // The bridge refused to take it: without this the call
                    // rejects while its timer and its bridge stay behind.
                    if (!done) {
                        finish();
                        reject(error instanceof Error ? error : new Error(String(error)));
                    }
                }
            }),

        subscribe: <Reply extends Payload>(uri: string, payload: Payload,
                                           onReply: (reply: Reply) => void,
                                           onError?: (error: LunaCallError | LunaTimeout) => void,
                                           options: LunaSubscribeOptions = {}): Subscription => {
            const bridge = take();
            let cancelled = false;
            let answered = false;
            // A service that is not there never answers at all. The first reply
            // is waited for like any other call; the ones after it are the
            // service's own business and are not timed.
            const firstReplyMs = options.firstReplyMs ?? DEFAULT_TIMEOUT_MS;
            const handle = firstReplyMs > 0
                ? timers.setTimeout(() => {
                    if (!cancelled && !answered) {
                        onError?.(new LunaTimeout(uri, firstReplyMs));
                    }
                }, firstReplyMs)
                : undefined;
            bridge.onservicecallback = (text) => {
                if (cancelled) {
                    return;
                }
                answered = true;
                timers.clearTimeout(handle);
                const reply = parse(text);
                if (failed(reply)) {
                    // Not the end of it: HP's services answer a transient
                    // failure and go on pushing.
                    deps.log?.(`${uri}: ${JSON.stringify(reply)}`);
                    onError?.(new LunaCallError(uri, reply));
                    return;
                }
                onReply(reply as Reply);
            };
            bridge.call(asBridgeUri(uri), JSON.stringify({ ...payload, subscribe: true }));
            return {
                cancel: () => {
                    if (cancelled) {
                        return;
                    }
                    cancelled = true;
                    timers.clearTimeout(handle);
                    give(bridge);
                },
            };
        },
    };
};

// The bridge WebAppMgr puts on the page, when there is one.
export const openPalmServiceBridge = (): PalmServiceBridge => {
    const make = (globalThis as { PalmServiceBridge?: new () => PalmServiceBridge }).PalmServiceBridge;
    if (!make) {
        throw new Error("PalmServiceBridge is not here: this page is not running in WebAppMgr");
    }
    return new make();
};
