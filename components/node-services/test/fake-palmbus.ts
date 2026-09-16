// An in-memory stand-in for palmbus.node, so the kit's tests run without a hub.
//
// It copies what the real addon and ls-hubd do, as far as luna.ts can tell:
//   * a call gets one reply; a subscribe call gets every reply;
//   * a call to a name nobody registered is answered by the "hub" in the error
//     category, with the text ls-hubd uses;
//   * a method name that is not an identifier throws at call time, as
//     LSCall's URI parser does;
//   * a subscriber cancelling makes the service's handle emit "cancel" with the
//     subscribed message, but only if the service added it with
//     subscriptionAdd;
//   * everything is delivered asynchronously, never inside the call.
// The same test suite runs against the real hub (luna.hub.test.ts), which is
// what keeps this copy honest.

import { HUB_ERROR_CATEGORY } from "#kit/luna.ts";
import type { OpenHandle, PalmCall, PalmHandle, PalmMessage } from "#kit/palmbus.ts";

type Listener = (message: PalmMessage) => void;

interface Endpoint {
    readonly methods: Set<string>;
    // Tokens of the requests the service added as subscriptions.
    readonly subscriptions: Set<string>;
    readonly emit: (event: "request" | "cancel", message: PalmMessage) => void;
}

const later = (work: () => void) => setImmediate(work);

const splitUri = (uri: string) => {
    const match = /^(?:luna|palm):\/\/([^/]+)(\/.*)?\/([^/]*)$/.exec(uri);
    if (!match || !/^[A-Za-z_][A-Za-z0-9_]*$/.test(match[3]!)) {
        throw new Error(`_UriParse: Not a valid method name in uri ${uri}`);
    }
    return { service: match[1]!, category: match[2] ?? "/", method: match[3]! };
};

const message = (fields: {
    payload: string; method?: string; category?: string; sender?: string; senderServiceName?: string | undefined;
    token?: string; subscription?: boolean; respond?: (payload: string) => void;
}): PalmMessage => ({
    payload: () => fields.payload,
    method: () => fields.method ?? "",
    category: () => fields.category ?? "",
    applicationID: () => undefined,
    sender: () => fields.sender,
    senderServiceName: () => fields.senderServiceName,
    uniqueToken: () => fields.token ?? "",
    isSubscription: () => fields.subscription ?? false,
    respond: (payload) => {
        fields.respond?.(payload);
        return true;
    },
});

export const createFakePalmbus = (): { openHandle: OpenHandle } => {
    const buses = { private: new Map<string, Endpoint>(), public: new Map<string, Endpoint>() };
    const counter = { value: 0 };

    const openHandle: OpenHandle = (name, publicBus) => {
        const bus = publicBus ? buses.public : buses.private;
        const listeners = { request: [] as Listener[], cancel: [] as Listener[] };
        const methods = new Set<string>();
        const unique = `fake.${++counter.value}`;
        const subscriptions = new Set<string>();
        const endpoint: Endpoint = {
            methods,
            subscriptions,
            emit: (event, delivered) => listeners[event].forEach((listener) => listener(delivered)),
        };
        if (name !== null) {
            bus.set(name, endpoint);
        }

        const send = (uri: string, payload: string, unlimited: boolean): PalmCall => {
            const target = splitUri(uri);
            const responses: Listener[] = [];
            const call = { open: true };
            const deliver = (delivered: PalmMessage) => later(() => {
                if (!call.open) {
                    return;
                }
                // One reply ends a plain call; a reply from the hub ends any call.
                if (!unlimited || delivered.category() === HUB_ERROR_CATEGORY) {
                    call.open = false;
                }
                responses.forEach((listener) => listener(delivered));
            });
            const token = `${unique}.${++counter.value}`;
            const service = bus.get(target.service);
            const methodPath = `${target.category.endsWith("/") ? target.category : `${target.category}/`}${target.method}`;
            const request = message({
                payload,
                method: target.method,
                category: target.category,
                sender: unique,
                senderServiceName: name ?? undefined,
                token,
                subscription: unlimited,
                respond: (reply) => deliver(message({ payload: reply, category: target.category })),
            });
            later(() => {
                if (!service || !service.methods.has(methodPath)) {
                    deliver(message({
                        payload: JSON.stringify({
                            returnValue: false,
                            errorCode: -1,
                            errorText: `Service does not exist: ${target.service}.`,
                        }),
                        category: HUB_ERROR_CATEGORY,
                    }));
                    return;
                }
                service.emit("request", request);
            });
            return {
                addListener: (_event, listener) => void responses.push(listener),
                cancel: () => {
                    // Only subscriptions the service added are reported.
                    if (call.open && service?.subscriptions.delete(token)) {
                        later(() => service.emit("cancel", request));
                    }
                    call.open = false;
                },
            };
        };

        const handle: PalmHandle = {
            call: (uri, payload) => send(uri, payload, false),
            subscribe: (uri, payload) => send(uri, payload, true),
            registerMethod: (category, method) =>
                void methods.add(`${category.endsWith("/") ? category : `${category}/`}${method}`),
            subscriptionAdd: (key) => void subscriptions.add(key),
            unregister: () => {
                if (name !== null && bus.get(name) === endpoint) {
                    bus.delete(name);
                }
            },
            addListener: (event, listener) => void listeners[event].push(listener),
        };
        return handle;
    };

    return { openHandle };
};
