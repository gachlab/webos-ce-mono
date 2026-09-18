// The Date & Time card's state machine, against a fake bus.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createFakeLuna, type FakeLuna } from "@webos/api/infra/luna/fake.service.ts";
import { createDateTimeService, filterZones } from "../src/dateandtime.service.ts";
import { formatClock } from "../src/luna/dateandtime.ts";
import type { Payload } from "@webos/api/infra/luna/service.ts";

const SYSTEM = "luna://com.palm.systemservice/";
const TIME = "luna://com.palm.systemservice/time/";

const settle = () => new Promise((resolve) => setImmediate(resolve));

const payloads = (luna: FakeLuna, uri: string): Payload[] =>
    luna.calls.filter((call) => call.uri === uri).map((call) => call.payload);

const setup = () => {
    const luna = createFakeLuna();
    const timers: Array<() => void> = [];
    luna.answer(`${SYSTEM}getPreferences`, () => ({
        returnValue: true,
        timeFormat: "HH12",
        useNetworkTime: true,
        useNetworkTimeZone: true,
        timeZone: { Country: "United States", City: "New York", Description: "Eastern" },
    }));
    luna.answer(`${SYSTEM}setPreferences`, () => ({ returnValue: true }));
    luna.answer(`${SYSTEM}getPreferenceValues`, () => ({
        returnValue: true,
        timeZone: [
            { Country: "United States", City: "New York", Description: "Eastern" },
            { Country: "Japan", City: "Tokyo", Description: "JST" },
        ],
    }));
    luna.answer(`${TIME}setSystemTime`, () => ({ returnValue: true }));
    const service = createDateTimeService({
        luna,
        setInterval: (callback) => {
            timers.push(callback);
            return timers.length;
        },
        clearInterval: () => {},
        now: () => new Date("2026-09-18T15:30:45"),
    });
    return { luna, service, data: () => service.getState().data, timers };
};

describe("clock formatting", () => {
    test("HH12 and HH24 labels differ for the same instant", () => {
        const date = new Date("2026-09-18T15:30:45");
        assert.match(formatClock(date, "HH12"), /3:30:45 PM/);
        assert.match(formatClock(date, "HH24"), /15:30:45/);
        assert.notEqual(formatClock(date, "HH12"), formatClock(date, "HH24"));
    });
});

describe("preferences", () => {
    test("loads prefs and ticks the clock", async () => {
        const { service, data, timers } = setup();
        service.onShown();
        await settle();
        assert.equal(data().timeFormat, "HH12");
        assert.equal(data().useNetworkTime, true);
        assert.ok(data().nowLabel.includes("3:30:45"));
        assert.equal(timers.length, 1);
    });

    test("Time Format writes timeFormat", async () => {
        const { luna, service, data } = setup();
        service.onShown();
        await settle();
        service.onTimeFormat("HH24");
        await settle();
        assert.equal(data().timeFormat, "HH24");
        assert.equal(payloads(luna, `${SYSTEM}setPreferences`)[0]?.timeFormat, "HH24");
    });

    test("manual time calls setSystemTime when network time is off", async () => {
        const { luna, service } = setup();
        service.onShown();
        await settle();
        service.onNetworkTime(false);
        service.onDateValue("2026-01-02");
        service.onTimeValue("08:15");
        service.onApplyManualTime();
        await settle();
        const sent = payloads(luna, `${TIME}setSystemTime`)[0];
        assert.ok(typeof sent?.utc === "number");
    });
});

describe("timezone list", () => {
    test("filterZones is case-insensitive and mutation-checked", () => {
        const zones = [
            { Country: "United States", City: "New York", Description: "Eastern", raw: {} },
            { Country: "Japan", City: "Tokyo", Description: "JST", raw: {} },
        ];
        assert.equal(filterZones(zones, "tok").length, 1);
        assert.equal(filterZones(zones, "tok")[0]?.City, "Tokyo");
        assert.notEqual(filterZones(zones, "tok")[0]?.City, "New York");
    });

    test("picking a zone writes timeZone preference", async () => {
        const { luna, service, data } = setup();
        service.onShown();
        await settle();
        service.onOpenTimezone();
        await settle();
        assert.equal(data().screen, "timezone");
        assert.equal(data().zones.length, 2);
        service.onPickTimezone(1);
        await settle();
        assert.equal(data().screen, "main");
        assert.equal(data().timeZone?.City, "Tokyo");
        assert.equal((payloads(luna, `${SYSTEM}setPreferences`).at(-1)?.timeZone as Payload).City, "Tokyo");
    });
});
