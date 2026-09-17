// The state every service keeps, and what a subscriber hears.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createState } from "#lib/helpers/create-state.ts";


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

    test("an error belongs to the state it happened in", () => {
        const state = createState<Networks & { joined?: string }>({
            name: "wifi:failed", data: { networks: [] }, error: "Wrong password",
        });
        state.patch({ joined: "home" });
        assert.equal(state.get().error, "Wrong password", "still failed, still the reason");
        state.patch({ networks: ["home"] }, "wifi:ready");
        assert.equal(state.get().error, undefined, "a new state is not still carrying the old failure");
    });

    test("a listener that changes the state does not make the next one skip this change", () => {
        const state = createState<Networks>({ name: "wifi:scanning", data: { networks: [] } });
        const second: string[] = [];
        let again = true;
        state.subscribe((s) => {
            if (s.name === "wifi:ready" && again) {
                again = false;
                state.set({ name: "wifi:off", data: { networks: [] } });
            }
        });
        state.subscribe((s) => second.push(s.name));
        state.set({ name: "wifi:ready", data: { networks: ["home"] } });
        assert.deepEqual(second, ["wifi:scanning", "wifi:off", "wifi:ready"],
                         "every subscriber hears every state, whatever order they arrive in");
    });

    test("clear lets every subscriber go, which is what disposing does", () => {
        const state = createState<Networks>({ name: "wifi:ready", data: { networks: [] } });
        const seen: string[] = [];
        state.subscribe((s) => seen.push(s.name));
        state.clear();
        state.set({ name: "wifi:off", data: { networks: [] } });
        assert.deepEqual(seen, ["wifi:ready"]);
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
