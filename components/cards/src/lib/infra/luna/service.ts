// The bus, as a card sees it: the port.
//
// A card asks a service something (`call`) or watches it (`subscribe`). What is
// underneath -- WebAppMgr's PalmServiceBridge, a fake in a test, nothing at all
// in a browser -- is chosen once, where the card is wired up, and never
// imported by the code that uses it.

// What crosses the bus. Payloads are JSON; the port does not pretend to know
// more about them than that.
export type Payload = Record<string, unknown>;

export interface LunaError {
    // HP's services answer with one or the other, and sometimes both.
    readonly errorCode?: number | string;
    readonly errorText?: string;
}

// A reply that failed, as its caller reads it: returnValue false, or an
// errorCode that is not 0 (com.palm.location and com.palm.storage answer that
// way, and enyo's PalmService treated both as failures).
export const failed = (reply: Payload): boolean =>
    reply.returnValue === false
    || (reply.errorCode !== undefined && reply.errorCode !== 0 && reply.errorCode !== "0");

export const errorTextOf = (reply: Payload): string => {
    const text = reply.errorText;
    if (typeof text === "string" && text) {
        return text;
    }
    const code = reply.errorCode;
    return code === undefined ? "the service did not say what went wrong" : `errorCode ${String(code)}`;
};

export interface LunaCallOptions {
    // Milliseconds before the call gives up. A card that waits forever shows a
    // spinner forever.
    readonly timeoutMs?: number;
}

export interface Subscription {
    cancel(): void;
}

export interface LunaSubscribeOptions {
    // Milliseconds to wait for the first reply. A service that is not there
    // never answers at all, and a card that waits for it shows its spinner for
    // good. 0 waits forever, for the rare caller that means it.
    readonly firstReplyMs?: number;
}

export interface LunaService {
    // One reply, or a rejection: LunaCallError when the service said no,
    // LunaTimeout when nothing answered in time.
    call<Reply extends Payload = Payload>(uri: string, payload?: Payload,
                                          options?: LunaCallOptions): Promise<Reply>;
    // Every reply, until the subscription is cancelled. A failed reply goes to
    // `onError`; the subscription stays, because HP's services answer a
    // transient failure and then carry on.
    subscribe<Reply extends Payload = Payload>(uri: string, payload: Payload,
                                               onReply: (reply: Reply) => void,
                                               onError?: (error: LunaCallError | LunaTimeout) => void,
                                               options?: LunaSubscribeOptions): Subscription;
}

export class LunaCallError extends Error {
    readonly uri: string;
    readonly reply: Payload;
    readonly errorCode: number | string | undefined;

    constructor(uri: string, reply: Payload) {
        super(`${uri}: ${errorTextOf(reply)}`);
        this.name = "LunaCallError";
        this.uri = uri;
        this.reply = reply;
        this.errorCode = reply.errorCode as number | string | undefined;
    }
}

export class LunaTimeout extends Error {
    readonly uri: string;

    constructor(uri: string, ms: number) {
        super(`${uri}: no answer after ${ms} ms`);
        this.name = "LunaTimeout";
        this.uri = uri;
    }
}

// How long a call waits when it does not say. HP's own services answer in
// milliseconds; a card that has waited this long is better off saying so.
export const DEFAULT_TIMEOUT_MS = 15_000;
