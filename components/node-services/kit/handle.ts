// What kit/luna.ts needs from the bus underneath: a connection that can call,
// answer and track subscriptions. kit/lunabus.ts provides it over the native
// addon; the tests provide an in-memory one.

// A message as it arrived: its fields, read once. `ref` keeps the native
// message alive so it can still be answered; nothing else should touch it.
export interface BusMessage {
    readonly payload: string;
    readonly method: string | undefined;
    readonly category: string | undefined;
    readonly sender: string | undefined;
    readonly senderServiceName: string | undefined;
    readonly applicationId: string | undefined;
    readonly uniqueToken: string | undefined;
    readonly isSubscription: boolean;
    readonly ref: unknown;
}

export type Token = number;

export interface BusHandle {
    // `oneReply` ends the call after its first reply; any other call lasts
    // until it is cancelled.
    call(uri: string, payload: string, oneReply: boolean, onResponse: (message: BusMessage) => void): Token;
    cancel(token: Token): void;
    registerMethod(category: string, method: string): void;
    // Tracks a subscribed request under `key`. luna-service2 only reports a
    // subscriber going away for requests added here.
    subscriptionAdd(key: string, message: BusMessage): void;
    respond(message: BusMessage, payload: string): boolean;
    close(): void;
}

export interface HandleListeners {
    readonly onRequest: (message: BusMessage) => void;
    readonly onCancel: (message: BusMessage) => void;
}

// Registers `name` (null for an anonymous client) on the private bus, or on the
// public one when `publicBus` is true.
export type OpenHandle = (name: string | null, publicBus: boolean, listeners: HandleListeners) => BusHandle;
