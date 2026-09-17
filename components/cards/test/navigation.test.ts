// Which screen a card is showing, and how it gets back.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createNavigation } from "#lib/services/navigation.service.ts";
import { createWatch } from "#lib/helpers/watch.ts";
import { createFakeLuna } from "#lib/infra/luna/fake.service.ts";
import { statusOf, createConnectionManager } from "#lib/infra/luna/connectionmanager.ts";

describe("navigation", () => {
    test("a screen opens on top of the one before, and back pops it", () => {
        const screens = createNavigation<string>("list");
        const seen: string[][] = [];
        screens.onChange((state) => seen.push([...state.data.stack]));
        screens.open("details");
        assert.equal(screens.now(), "details");
        assert.deepEqual(screens.stack(), ["list", "details"]);
        assert.equal(screens.back(), true);
        assert.equal(screens.now(), "list");
        assert.deepEqual(seen, [["list"], ["list", "details"], ["list"]]);
    });

    test("the first screen is not popped: that is the card closing", () => {
        const screens = createNavigation<string>("list");
        assert.equal(screens.back(), false);
        assert.deepEqual(screens.stack(), ["list"]);
    });

    test("a relaunch replaces the screen on top rather than stacking another", () => {
        const screens = createNavigation<string>("list");
        screens.open("details");
        screens.show("other details");
        assert.deepEqual(screens.stack(), ["list", "other details"]);
    });
});

describe("a subscription that follows the card", () => {
    test("starts once, stops when told, and starts again", () => {
        const luna = createFakeLuna();
        const watch = createWatch(() => luna.subscribe("luna://com.palm.wifi/getstatus", {}, () => {}));
        watch.start();
        watch.start();
        assert.equal(luna.subscribers.length, 1, "asked twice, subscribed once");
        assert.equal(watch.watching(), true);
        watch.stop();
        assert.deepEqual(luna.subscribers, []);
        assert.equal(watch.watching(), false);
        watch.stop();
        watch.start();
        assert.equal(luna.subscribers.length, 1);
    });
});

describe("com.palm.connectionmanager, typed", () => {
    test("its reply becomes what a card shows", () => {
        assert.deepEqual(statusOf({
            isInternetConnectionAvailable: true,
            wifi: { state: "connected", ssid: "home", ipAddress: "192.168.1.10" },
            wired: { state: "disconnected" },
        }), { online: true, through: "wifi", ssid: "home", ipAddress: "192.168.1.10" });

        assert.deepEqual(statusOf({
            isInternetConnectionAvailable: true,
            wifi: { state: "disconnected", ssid: "home" },
            wired: { state: "connected", ipAddress: "10.0.0.2" },
        }), { online: true, through: "wired", ssid: "home", ipAddress: "10.0.0.2" });

        // Nothing connected, and a service that answers with nothing at all.
        assert.deepEqual(statusOf({ isInternetConnectionAvailable: false }),
                         { online: false, through: "", ssid: "", ipAddress: "" });
        assert.deepEqual(statusOf({}), { online: false, through: "", ssid: "", ipAddress: "" });
    });

    test("the card asks the service by name once, here", async () => {
        const luna = createFakeLuna();
        luna.answer("luna://com.palm.connectionmanager/getstatus", () => ({
            returnValue: true, isInternetConnectionAvailable: true, wifi: { state: "connected", ssid: "home" },
        }));
        const manager = createConnectionManager(luna);
        assert.equal((await manager.status()).ssid, "home");

        const seen: boolean[] = [];
        const watching = manager.watchStatus((status) => seen.push(status.online));
        luna.subscribers[0]!.push({ returnValue: true, isInternetConnectionAvailable: false });
        watching.cancel();
        assert.deepEqual(seen.at(-1), false);
        assert.deepEqual(luna.calls.map((c) => c.uri),
                         ["luna://com.palm.connectionmanager/getstatus",
                          "luna://com.palm.connectionmanager/getstatus"]);
    });
});
