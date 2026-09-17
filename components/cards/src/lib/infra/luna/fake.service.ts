// The bus with no bus: what a card talks to in a test, and in a browser where
// there is no WebAppMgr.
//
// A handler answers a uri. `reply` answers once; `push` keeps the subscription
// and lets the test send whatever the service would have sent, whenever it
// chooses -- which is how a card's "the network changed under you" path gets
// tested without a hub.

import { LunaCallError, failed, type LunaService, type Payload, type Subscription } from "./service.ts";

export interface FakeCall {
    readonly uri: string;
    readonly payload: Payload;
    readonly subscribed: boolean;
}

export interface FakeSubscriber {
    readonly uri: string;
    readonly payload: Payload;
    // Sends a reply, as the service would.
    push(reply: Payload): void;
    readonly cancelled: () => boolean;
}

export interface FakeLuna extends LunaService {
    // Every call and subscription, in order.
    readonly calls: FakeCall[];
    // The subscriptions still open.
    readonly subscribers: FakeSubscriber[];
    // What answers `uri`. A function may return a payload or a promise, or
    // throw to fail the call.
    answer(uri: string, handler: (payload: Payload) => Payload | Promise<Payload>): void;
    // A uri nothing answers: the call never settles, which is what a card
    // waiting on a service that is not there really sees.
    silence(uri: string): void;
}

export const createFakeLuna = (): FakeLuna => {
    const answers = new Map<string, (payload: Payload) => Payload | Promise<Payload>>();
    const silent = new Set<string>();
    const calls: FakeCall[] = [];
    const subscribers: FakeSubscriber[] = [];

    const fake: FakeLuna = {
        calls,
        subscribers,
        answer: (uri, handler) => {
            answers.set(uri, handler);
            silent.delete(uri);
        },
        silence: (uri) => {
            silent.add(uri);
            answers.delete(uri);
        },
        call: async <Reply extends Payload>(uri: string, payload: Payload = {}): Promise<Reply> => {
            calls.push({ uri, payload, subscribed: false });
            if (silent.has(uri)) {
                return new Promise<Reply>(() => {});
            }
            const handler = answers.get(uri);
            if (!handler) {
                throw new LunaCallError(uri, { returnValue: false, errorText: `Service does not exist: ${uri}` });
            }
            const reply = await handler(payload);
            if (failed(reply)) {
                throw new LunaCallError(uri, reply);
            }
            return reply as Reply;
        },
        subscribe: <Reply extends Payload>(uri: string, payload: Payload,
                                           onReply: (reply: Reply) => void,
                                           onError?: (error: LunaCallError) => void): Subscription => {
            calls.push({ uri, payload, subscribed: true });
            let cancelled = false;
            const subscriber: FakeSubscriber = {
                uri,
                payload,
                cancelled: () => cancelled,
                push: (reply) => {
                    if (cancelled) {
                        return;
                    }
                    if (failed(reply)) {
                        onError?.(new LunaCallError(uri, reply));
                        return;
                    }
                    onReply(reply as Reply);
                },
            };
            subscribers.push(subscriber);
            const handler = answers.get(uri);
            if (handler) {
                void Promise.resolve(handler(payload)).then((reply) => subscriber.push(reply));
            }
            return {
                cancel: () => {
                    cancelled = true;
                },
            };
        },
    };
    return fake;
};
