// The bus a card talks to: the bridge WebAppMgr gives it, and the fake that
// stands in for it.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createBridgeLuna, type PalmServiceBridge } from "#lib/infra/luna/bridge.service.ts";
import { createFakeLuna } from "#lib/infra/luna/fake.service.ts";
import { LunaCallError, LunaTimeout, errorTextOf, failed, type Payload } from "#lib/infra/luna/service.ts";
import type { Timers } from "#lib/helpers/with-deadline.ts";

// A stand-in for WebAppMgr's bridge: it records what it was asked and answers
// when the test says so.
const fakeBridges = () => {
    interface Opened {
        uri?: string;
        payload?: Payload;
        cancelled: boolean;
        answer(text: string): void;
        bridge: PalmServiceBridge;
    }
    const opened: Opened[] = [];
    const open = (): PalmServiceBridge => {
        const made: Opened = {
            cancelled: false,
            answer: (text: string) => made.bridge.onservicecallback?.(text),
            bridge: {
                onservicecallback: null,
                call: (uri: string, payload: string) => {
                    made.uri = uri;
                    made.payload = JSON.parse(payload) as Payload;
                },
                cancel: () => { made.cancelled = true; },
            } as PalmServiceBridge,
        };
        opened.push(made);
        return made.bridge;
    };
    return { open, opened };
};

const immediateTimers = (): Timers & { fire: () => void } => {
    let pending: (() => void) | undefined;
    return {
        setTimeout: (callback) => {
            pending = callback;
            return 1;
        },
        clearTimeout: () => { pending = undefined; },
        fire: () => pending?.(),
    };
};

describe("a reply that failed", () => {
    test("is what its caller reads as one", () => {
        assert.equal(failed({ returnValue: true }), false);
        assert.equal(failed({ returnValue: true, errorCode: 0 }), false);
        assert.equal(failed({ returnValue: false }), true);
        assert.equal(failed({ errorCode: 8 }), true, "com.palm.location and com.palm.storage answer this way");
        assert.equal(errorTextOf({ errorText: "no usb connection" }), "no usb connection");
        assert.equal(errorTextOf({ errorCode: 8 }), "errorCode 8");
        assert.match(errorTextOf({}), /did not say/);
    });
});

describe("the bridge", () => {
    test("a call goes out as palm:// with its payload, and the reply comes back parsed", async () => {
        const bridges = fakeBridges();
        const luna = createBridgeLuna({ open: bridges.open });
        const reply = luna.call("luna://com.palm.wifi/findnetworks", { subscribe: false });
        const call = bridges.opened[0]!;
        assert.equal(call.uri, "palm://com.palm.wifi/findnetworks");
        assert.deepEqual(call.payload, { subscribe: false });
        call.answer(JSON.stringify({ returnValue: true, foundNetworks: [{ ssid: "home" }] }));
        assert.deepEqual((await reply).foundNetworks, [{ ssid: "home" }]);
        assert.equal(call.cancelled, true, "the bridge is let go when the call is over");
    });

    test("a service that says no is an error the card can read", async () => {
        const bridges = fakeBridges();
        const luna = createBridgeLuna({ open: bridges.open });
        const reply = luna.call("luna://com.palm.wifi/connect", { ssid: "home" });
        bridges.opened[0]!.answer(JSON.stringify({ returnValue: false, errorCode: 5, errorText: "Wifi is off" }));
        const error = await reply.then(() => undefined, (e: unknown) => e);
        assert.ok(error instanceof LunaCallError);
        assert.equal(error.errorCode, 5);
        assert.match(error.message, /Wifi is off/);
    });

    test("a service that never answers is given up on, not waited on forever", async () => {
        const bridges = fakeBridges();
        const timers = immediateTimers();
        const luna = createBridgeLuna({ open: bridges.open, timers });
        const reply = luna.call("luna://com.palm.wifi/getstatus", {}, { timeoutMs: 2000 });
        timers.fire();
        const error = await reply.then(() => undefined, (e: unknown) => e);
        assert.ok(error instanceof LunaTimeout);
        assert.match(error.message, /2000 ms/);
        assert.equal(bridges.opened[0]!.cancelled, true);
        // The answer that arrives after it is not the card's problem any more.
        bridges.opened[0]!.answer(JSON.stringify({ returnValue: true }));
    });

    test("a subscription asks to subscribe, keeps every reply, and ends when cancelled", () => {
        const bridges = fakeBridges();
        const luna = createBridgeLuna({ open: bridges.open });
        const replies: Payload[] = [];
        const errors: string[] = [];
        const watching = luna.subscribe("luna://com.palm.connectionmanager/getstatus", {},
                                        (reply) => replies.push(reply), (error) => errors.push(error.message));
        const call = bridges.opened[0]!;
        assert.deepEqual(call.payload, { subscribe: true });
        call.answer(JSON.stringify({ returnValue: true, isInternetConnectionAvailable: true }));
        call.answer(JSON.stringify({ returnValue: false, errorText: "briefly gone" }));
        call.answer(JSON.stringify({ returnValue: true, isInternetConnectionAvailable: false }));
        watching.cancel();
        call.answer(JSON.stringify({ returnValue: true, isInternetConnectionAvailable: true }));
        assert.deepEqual(replies.map((r) => r.isInternetConnectionAvailable), [true, false],
                         "a failure in the middle does not end the subscription, and nothing arrives after cancel");
        assert.deepEqual(errors.length, 1);
        assert.equal(call.cancelled, true);
    });

    test("a reply that is not JSON is an empty one, not a broken card", async () => {
        const bridges = fakeBridges();
        const luna = createBridgeLuna({ open: bridges.open });
        const reply = luna.call("luna://com.palm.wifi/getstatus");
        bridges.opened[0]!.answer("<html>not json</html>");
        assert.deepEqual(await reply, {});
    });
});

describe("the fake bus", () => {
    test("answers what a test tells it to, and records what was asked", async () => {
        const luna = createFakeLuna();
        luna.answer("luna://com.palm.wifi/getstatus", () => ({ returnValue: true, status: "serviceEnabled" }));
        const reply = await luna.call("luna://com.palm.wifi/getstatus", { detailed: true });
        assert.equal(reply.status, "serviceEnabled");
        assert.deepEqual(luna.calls[0], { uri: "luna://com.palm.wifi/getstatus", payload: { detailed: true }, subscribed: false });
    });

    test("a uri nothing answers fails the way the hub fails it", async () => {
        const luna = createFakeLuna();
        const error = await luna.call("luna://com.palm.nothing/at/all").then(() => undefined, (e: unknown) => e);
        assert.ok(error instanceof LunaCallError);
        assert.match(error.message, /does not exist/);
    });

    test("a service that is there but says no throws it", async () => {
        const luna = createFakeLuna();
        luna.answer("luna://com.palm.wifi/connect", () => ({ returnValue: false, errorText: "Wifi is off" }));
        await assert.rejects(luna.call("luna://com.palm.wifi/connect"), /Wifi is off/);
    });

    test("a subscription can be pushed to, as the real service pushes", () => {
        const luna = createFakeLuna();
        const replies: Payload[] = [];
        const watching = luna.subscribe("luna://com.palm.wifi/getstatus", {}, (reply) => replies.push(reply));
        luna.subscribers[0]!.push({ returnValue: true, status: "serviceEnabled" });
        watching.cancel();
        luna.subscribers[0]!.push({ returnValue: true, status: "serviceDisabled" });
        assert.deepEqual(replies.map((r) => r.status), ["serviceEnabled"]);
        assert.equal(luna.subscribers[0]!.cancelled(), true);
    });

    test("a silent uri never answers, which is what a missing service looks like", async () => {
        const luna = createFakeLuna();
        luna.silence("luna://com.palm.storage/diskmode/hostIsConnected");
        let settled = false;
        void luna.call("luna://com.palm.storage/diskmode/hostIsConnected").then(() => { settled = true; },
                                                                                () => { settled = true; });
        await new Promise((resolve) => setImmediate(resolve));
        assert.equal(settled, false);
    });
});
