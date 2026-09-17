// The three pieces every service is built from.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createState } from "#lib/helpers/create-state.ts";
import { createEventBus } from "#lib/helpers/event-bus.ts";
import { TIMED_OUT, timedOut, withDeadline, type Timers } from "#lib/helpers/with-deadline.ts";

interface Networks {
    readonly networks: string[];
}

describe("state", () => {
    test("a new subscriber hears the state at once, so a card can paint", () => {
        const state = createState<Networks>({ name: "wifi:scanning", data: { networks: [] } });
        const seen: string[] = [];
        state.subscribe((s) => seen.push(s.name));
        assert.deepEqual(seen, ["wifi:scanning"]);
    });

    test("every change reaches every subscriber, until it leaves", () => {
        const state = createState<Networks>({ name: "wifi:scanning", data: { networks: [] } });
        const first: string[] = [];
        const second: string[] = [];
        const stop = state.subscribe((s) => first.push(s.name));
        state.subscribe((s) => second.push(s.name));
        state.set({ name: "wifi:ready", data: { networks: ["home"] } });
        stop();
        state.set({ name: "wifi:off", data: { networks: [] } });
        assert.deepEqual(first, ["wifi:scanning", "wifi:ready"]);
        assert.deepEqual(second, ["wifi:scanning", "wifi:ready", "wifi:off"]);
        assert.deepEqual(state.get(), { name: "wifi:off", data: { networks: [] } });
    });

    test("patch keeps the name unless it is given a new one", () => {
        const state = createState<Networks & { joined?: string }>({ name: "wifi:ready", data: { networks: ["home"] } });
        state.patch({ joined: "home" });
        assert.deepEqual(state.get(), { name: "wifi:ready", data: { networks: ["home"], joined: "home" } });
        state.patch({ networks: [] }, "wifi:off");
        assert.equal(state.get().name, "wifi:off");
        assert.equal(state.get().data.joined, "home");
    });

    test("a subscriber that leaves while the change is going round still stops after it", () => {
        const state = createState<Networks>({ name: "wifi:ready", data: { networks: [] } });
        const seen: string[] = [];
        let stopOther = () => {};
        state.subscribe(() => stopOther());
        stopOther = state.subscribe((s) => seen.push(s.name));
        state.set({ name: "wifi:off", data: { networks: [] } });
        assert.deepEqual(seen, ["wifi:ready", "wifi:off"], "the round in progress is not cut short");
        state.set({ name: "wifi:ready", data: { networks: [] } });
        assert.deepEqual(seen, ["wifi:ready", "wifi:off"], "and it is gone for the next one");
    });
});

describe("event bus", () => {
    test("what one service says reaches whoever is listening", () => {
        const bus = createEventBus();
        const heard: unknown[] = [];
        const stop = bus.on("wifi:joined", (payload) => heard.push(payload));
        bus.on("wifi:joined", () => heard.push("second"));
        bus.emit("wifi:joined", { ssid: "home" });
        stop();
        bus.emit("wifi:joined", { ssid: "other" });
        bus.emit("nobody:listening");
        assert.deepEqual(heard, [{ ssid: "home" }, "second", "second"]);
        bus.clear();
        bus.emit("wifi:joined", {});
        assert.equal(heard.length, 3);
    });
});

describe("deadlines", () => {
    const fakeTimers = () => {
        const due: { at: number; callback: () => void; handle: number }[] = [];
        let now = 0;
        let next = 1;
        const timers: Timers = {
            setTimeout: (callback, ms) => {
                const handle = next++;
                due.push({ at: now + ms, callback, handle });
                return handle;
            },
            clearTimeout: (handle) => {
                const at = due.findIndex((timer) => timer.handle === handle);
                if (at >= 0) {
                    due.splice(at, 1);
                }
            },
        };
        return {
            timers,
            pending: () => due.length,
            advance: (ms: number) => {
                now += ms;
                for (const timer of [...due].filter((t) => t.at <= now)) {
                    timers.clearTimeout(timer.handle);
                    timer.callback();
                }
            },
        };
    };

    test("work that answers in time is the answer, and the clock is cleared", async () => {
        const clock = fakeTimers();
        const outcome = await withDeadline(Promise.resolve("ready"), 1000, undefined, clock.timers);
        assert.equal(outcome, "ready");
        assert.equal(timedOut(outcome), false);
        assert.equal(clock.pending(), 0, "nothing is left ticking");
    });

    test("work that does not is given up on, and told so", async () => {
        const clock = fakeTimers();
        let cancelled = false;
        const outcome = withDeadline(new Promise<string>(() => {}), 5000, () => { cancelled = true; }, clock.timers);
        clock.advance(5000);
        assert.equal(await outcome, TIMED_OUT);
        assert.equal(timedOut(await outcome), true);
        assert.equal(cancelled, true, "whatever was in flight is cancelled");
        assert.equal(clock.pending(), 0);
    });
});
