// An in-memory stand-in for the native bus, so the kit's tests run without a hub.
//
// It copies what lunabus.node and ls-hubd do, as far as luna.ts can tell:
//   * a one-reply call gets one reply; any other call gets every reply;
//   * a call to a name nobody registered, or to a method the service does not
//     have, is answered by the "hub" in its error category;
//   * a method name that is not an identifier throws at call time, as
//     luna-service2's URI parser does;
//   * cancelling a call makes the service's handle report the cancel, but only
//     for a request the service added with subscriptionAdd;
//   * everything is delivered asynchronously, never inside the call.
// The same suite runs against the real hub (luna.hub.test.ts), which is what
// keeps this copy honest.

import { HUB_ERROR_CATEGORY } from "#kit/luna.ts";
import type { BusMessage, HandleListeners, OpenHandle, Token } from "#kit/handle.ts";

interface Endpoint {
    readonly methods: Set<string>;
    // Tokens of the requests the service added as subscriptions.
    readonly subscriptions: Set<string>;
    readonly listeners: HandleListeners;
    readonly state: { closed: boolean };
}

// What a fake message's `ref` carries: who answers it.
interface FakeRef {
    readonly deliver: (payload: string) => void;
    readonly answerer: Endpoint | undefined;
}

const later = (work: () => void) => void setImmediate(work);

const methodPath = (category: string, method: string) =>
    `${category.endsWith("/") ? category : `${category}/`}${method}`;

const splitUri = (uri: string) => {
    const match = /^(?:luna|palm):\/\/([^/]+)(\/.*)?\/([^/]*)$/.exec(uri);
    if (!match || !/^[A-Za-z_][A-Za-z0-9_]*$/.test(match[3]!)) {
        throw new Error(`LSCall: Not a valid method name in uri ${uri}`);
    }
    return { service: match[1]!, category: match[2] ?? "/", method: match[3]! };
};

const wantsSubscription = (payload: string): boolean => {
    try {
        return (JSON.parse(payload) as { subscribe?: unknown }).subscribe === true;
    } catch {
        return false;
    }
};

const message = (fields: Partial<BusMessage> & { payload: string; ref: FakeRef }): BusMessage => ({
    method: undefined,
    category: undefined,
    sender: undefined,
    senderServiceName: undefined,
    applicationId: undefined,
    uniqueToken: undefined,
    isSubscription: false,
    ...fields,
});

export const createFakeBus = (): { openHandle: OpenHandle } => {
    const buses = { private: new Map<string, Endpoint>(), public: new Map<string, Endpoint>() };
    const counter = { value: 0 };
    const nowhere: FakeRef = { deliver: () => {}, answerer: undefined };

    const openHandle: OpenHandle = (name, publicBus, listeners) => {
        const bus = publicBus ? buses.public : buses.private;
        const unique = `fake.${++counter.value}`;
        const endpoint: Endpoint = { methods: new Set(), subscriptions: new Set(), listeners, state: { closed: false } };
        if (name !== null) {
            bus.set(name, endpoint);
        }
        const calls = new Map<Token, { open: boolean; cancel: () => void }>();

        const call = (uri: string, payload: string, oneReply: boolean, onResponse: (m: BusMessage) => void): Token => {
            const target = splitUri(uri);
            const token = ++counter.value;
            const requestToken = `${unique}.${token}`;
            const service = bus.get(target.service);
            const entry = { open: true, cancel: () => {} };
            calls.set(token, entry);

            const deliver = (reply: BusMessage) => later(() => {
                if (!entry.open || endpoint.state.closed) {
                    return;
                }
                if (oneReply) {
                    entry.open = false;
                }
                onResponse(reply);
            });
            const request = message({
                payload,
                method: target.method,
                category: target.category,
                sender: unique,
                senderServiceName: name ?? undefined,
                uniqueToken: requestToken,
                isSubscription: wantsSubscription(payload),
                ref: {
                    deliver: (reply) => deliver(message({ payload: reply, category: target.category, ref: nowhere })),
                    answerer: service,
                },
            });
            entry.cancel = () => {
                if (entry.open && service?.subscriptions.delete(requestToken)) {
                    later(() => service.listeners.onCancel(request));
                }
                entry.open = false;
            };
            later(() => {
                if (!service || service.state.closed || !service.methods.has(methodPath(target.category, target.method))) {
                    deliver(message({
                        payload: JSON.stringify({
                            returnValue: false,
                            errorCode: -1,
                            errorText: `Service does not exist: ${target.service}.`,
                        }),
                        category: HUB_ERROR_CATEGORY,
                        ref: nowhere,
                    }));
                    return;
                }
                service.listeners.onRequest(request);
            });
            return token;
        };

        return {
            call,
            cancel: (token) => calls.get(token)?.cancel(),
            registerMethod: (category, method) => void endpoint.methods.add(methodPath(category, method)),
            subscriptionAdd: (_key, request) => void endpoint.subscriptions.add(request.uniqueToken ?? ""),
            respond: (request, payload) => {
                const ref = request.ref as FakeRef;
                if (ref.answerer?.state.closed) {
                    return false;
                }
                ref.deliver(payload);
                return true;
            },
            close: () => {
                endpoint.state.closed = true;
                if (name !== null && bus.get(name) === endpoint) {
                    bus.delete(name);
                }
            },
        };
    };

    return { openHandle };
};
