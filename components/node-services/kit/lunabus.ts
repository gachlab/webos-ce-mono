// The native addon (native/lunabus.cpp), typed and shaped as an OpenHandle.

import { createRequire } from "node:module";

import type { BusMessage, OpenHandle, Token } from "./handle.ts";

type NativeHandle = { readonly __nativeHandle: unique symbol };

export interface Lunabus {
    open(name: string | null, publicBus: boolean,
         onRequest: (message: BusMessage) => void, onCancel: (message: BusMessage) => void): NativeHandle;
    call(handle: NativeHandle, uri: string, payload: string, oneReply: boolean,
         onResponse: (message: BusMessage) => void): Token;
    cancel(handle: NativeHandle, token: Token): void;
    registerMethod(handle: NativeHandle, category: string, method: string): void;
    subscriptionAdd(handle: NativeHandle, key: string, message: BusMessage): void;
    respond(message: BusMessage, payload: string): boolean;
    close(handle: NativeHandle): void;
}

export const once = <T>(make: () => T): (() => T) => {
    let made: { value: T } | undefined;
    return () => (made ??= { value: make() }).value;
};

// WEBOS_LUNABUS names the addon directly, for running from the source tree.
// Otherwise it is found through NODE_PATH, which the session sets to
// /usr/palm/nodejs, where the build installs it.
export const requireLunabus = (specifier: string = process.env.WEBOS_LUNABUS ?? "lunabus.node") =>
    once(() => createRequire(import.meta.url)(specifier) as Lunabus);

export const createOpenHandle = (load: () => Lunabus): OpenHandle => (name, publicBus, listeners) => {
    const lib = load();
    const native = lib.open(name, publicBus, listeners.onRequest, listeners.onCancel);
    return {
        call: (uri, payload, oneReply, onResponse) => lib.call(native, uri, payload, oneReply, onResponse),
        cancel: (token) => lib.cancel(native, token),
        registerMethod: (category, method) => lib.registerMethod(native, category, method),
        subscriptionAdd: (key, message) => lib.subscriptionAdd(native, key, message),
        respond: (message, payload) => lib.respond(message, payload),
        close: () => lib.close(native),
    };
};

export const openHandle: OpenHandle = createOpenHandle(requireLunabus());
