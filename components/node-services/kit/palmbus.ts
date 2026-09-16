// The native bus addon, typed, behind a function the rest of the kit receives.
//
// palmbus.node is HP's nodejs-module-webos-sysbus, built unmodified on
// components/node-v8-shim. Its API is callbacks and strings; luna.ts is what the
// services use. Tests hand luna.ts a fake OpenHandle instead of this one.

import { createRequire } from "node:module";

export interface PalmMessage {
    payload(): string;
    method(): string;
    category(): string;
    applicationID(): string | undefined;
    sender(): string | undefined;
    senderServiceName(): string | undefined;
    uniqueToken(): string;
    isSubscription(): boolean;
    respond(payload: string): boolean;
}

export interface PalmCall {
    addListener(event: "response", listener: (message: PalmMessage) => void): void;
    cancel(): void;
}

export interface PalmHandle {
    call(uri: string, payload: string): PalmCall;
    subscribe(uri: string, payload: string): PalmCall;
    registerMethod(category: string, method: string): void;
    // Tracks a subscribed request under `key`. luna-service2 only reports a
    // subscriber going away (the "cancel" event) for requests added here.
    subscriptionAdd(key: string, message: PalmMessage): void;
    unregister(): void;
    addListener(event: "request" | "cancel", listener: (message: PalmMessage) => void): void;
}

export interface Palmbus {
    Handle: new (name: string | null, publicBus: boolean) => PalmHandle;
}

// Registers `name` (null for an anonymous client) on the private bus, or on the
// public one when `publicBus` is true.
export type OpenHandle = (name: string | null, publicBus: boolean) => PalmHandle;

export const once = <T>(make: () => T): (() => T) => {
    let made: { value: T } | undefined;
    return () => (made ??= { value: make() }).value;
};

// WEBOS_PALMBUS names the addon directly, for running from the source tree.
// Otherwise it is found the way HP's services found it: require("palmbus")
// through NODE_PATH, which the session sets to /usr/palm/nodejs.
export const requirePalmbus = (specifier: string = process.env.WEBOS_PALMBUS ?? "palmbus") =>
    once(() => createRequire(import.meta.url)(specifier) as Palmbus);

export const createOpenHandle = (load: () => Palmbus): OpenHandle =>
    (name, publicBus) => new (load().Handle)(name, publicBus);

export const openHandle: OpenHandle = createOpenHandle(requirePalmbus());
