// The bus through WebAppMgr's PalmServiceBridge, which is what a card has.
//
// The bridge is one call: `new PalmServiceBridge()`, `call(uri, json)`, replies
// arrive as JSON text on `onservicecallback`, and `cancel()` ends it. A bridge
// carries one call, so this makes one per call and throws it away when the call
// is over -- which is what enyo's PalmService did, and what keeps a cancelled
// subscription from being answered into a card that has moved on.

import {
    DEFAULT_TIMEOUT_MS, LunaCallError, LunaTimeout,
    failed, type LunaCallOptions, type LunaService, type Payload, type Subscription,
} from "./service.ts";
import { systemTimers, type Timers } from "#lib/helpers/with-deadline.ts";

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
    return {
        call: <Reply extends Payload>(uri: string, payload: Payload = {}, options: LunaCallOptions = {}) =>
            new Promise<Reply>((resolve, reject) => {
                const ms = options.timeoutMs ?? DEFAULT_TIMEOUT_MS;
                const bridge = deps.open();
                let done = false;
                const finish = () => {
                    done = true;
                    timers.clearTimeout(handle);
                    bridge.onservicecallback = null;
                    bridge.cancel();
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
                bridge.call(asBridgeUri(uri), JSON.stringify(payload));
            }),

        subscribe: <Reply extends Payload>(uri: string, payload: Payload,
                                           onReply: (reply: Reply) => void,
                                           onError?: (error: LunaCallError) => void): Subscription => {
            const bridge = deps.open();
            let cancelled = false;
            bridge.onservicecallback = (text) => {
                if (cancelled) {
                    return;
                }
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
                    bridge.onservicecallback = null;
                    bridge.cancel();
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
