// com.palm.location on a private hub: the replies HP's callers read, the
// preferences, and the system UI's location alert, round trip.

import assert from "node:assert/strict";
import { after, before, beforeEach, describe, test } from "node:test";
import { setTimeout as sleep } from "node:timers/promises";

import { isLunaError, openBus, type Bus, type Payload } from "#kit/luna.ts";
import { ERRORS } from "../services/com.palm.location/locator.ts";
import { createLocationService, SERVICE_NAME, type RunningLocation } from "../services/com.palm.location/service.ts";
import { UNKNOWN, type Fix, type Source } from "../services/com.palm.location/sources.ts";
import { startTestBus, type TestBus } from "./hub.ts";

const SYSTEM_MANAGER = "com.palm.systemmanager";
const APP_MANAGER = "com.palm.applicationManager";
const L = `luna://${SERVICE_NAME}`;

const env = {} as {
    hub: TestBus;
    client: Bus;
    systemManager: Bus;
    appManager: Bus;
    service: RunningLocation;
    published: Payload[];
    stored: string | undefined;
    gps: Fix | undefined;
    wifi: Fix | undefined;
    lookups: string[];
};

const fixAt = (latitude: number, longitude: number, horizAccuracy: number): Fix => ({
    latitude, longitude, horizAccuracy, vertAccuracy: UNKNOWN, altitude: UNKNOWN,
    heading: UNKNOWN, velocity: UNKNOWN, timestamp: 1_700_000_000_000,
});

const fake = (name: "gps" | "wifi", read: () => Fix | undefined): Source => ({
    name,
    remote: name !== "gps",
    locate: async () => {
        env.lookups.push(name);
        return read();
    },
});

const failsWith = (errorCode: number) => (error: unknown) => {
    assert.ok(isLunaError(error), String(error));
    assert.equal((error as unknown as Payload).errorCode, errorCode);
    return true;
};

const waitFor = async (ready: () => boolean) => {
    for (let i = 0; i < 100 && !ready(); i++) {
        await sleep(20);
    }
    assert.ok(ready(), "never happened");
};

before(async () => {
    env.hub = await startTestBus({ services: [SERVICE_NAME, SYSTEM_MANAGER, APP_MANAGER] });
    env.published = [];
    env.lookups = [];
    env.systemManager = openBus(SYSTEM_MANAGER);
    env.systemManager.method("publishToSystemUI", ({ payload }) => {
        env.published.push(payload);
        return {};
    });
    env.appManager = openBus(APP_MANAGER);
    env.appManager.method("getAppInfo", () => ({ returnValue: false, errorText: "no such app" }));
    env.service = createLocationService({
        openBus,
        sources: [fake("gps", () => env.gps), fake("wifi", () => env.wifi)],
        prefsFile: { read: () => env.stored, write: (text) => { env.stored = text; } },
        reverse: async (latitude, longitude) => ({
            address: `${latitude};${longitude}`, street: "", substreet: "", zipcode: "", country: "",
        }),
        gpsAvailable: async () => true,
        now: () => 1_700_000_000_000,
        sleep: (ms, signal) => sleep(ms, undefined, { signal }),
        trackMs: 20,
        log: () => {},
    })();
    env.client = openBus(null);
});

after(() => {
    env.service?.close();
    env.client?.close();
    env.systemManager?.close();
    env.appManager?.close();
    env.hub?.stop();
});

beforeEach(async () => {
    env.published.length = 0;
    env.lookups.length = 0;
    env.gps = fixAt(1, 2, 5);
    env.wifi = undefined;
    await env.client.call(`${L}/rejectTermsOfUse`, {});
    await env.client.call(`${L}/setUseGps`, { useGps: true });
    await env.client.call(`${L}/setAutoLocate`, { autoLocate: false });
    await env.client.call(`${L}/setWebSetting`, { webSetting: true });
    await env.client.call(`${L}/clearWebSetting`, {});
});

describe("com.palm.location", () => {
    test("getCurrentPosition answers the system with HP's fields", async () => {
        const reply = await env.client.call(`${L}/getCurrentPosition`, { accuracy: 1, responseTime: 1 });
        assert.deepEqual(reply, {
            returnValue: true, errorCode: 0, timestamp: 1_700_000_000_000,
            latitude: 1, longitude: 2, horizAccuracy: 5, vertAccuracy: -1, altitude: -1, heading: -1, velocity: -1,
        });
        assert.equal(env.published.length, 0, "the system is not asked about");
    });

    test("with every source off, the error is HP's; the network waits for the terms", async () => {
        await env.client.call(`${L}/setUseGps`, { useGps: false });
        await assert.rejects(env.client.call(`${L}/getCurrentPosition`, {}), failsWith(ERRORS.serviceOff));
        await env.client.call(`${L}/setUseGoogle`, { useGoogle: true });
        await assert.rejects(env.client.call(`${L}/getCurrentPosition`, {}), failsWith(ERRORS.termsNotAccepted));
        env.wifi = fixAt(3, 4, 30);
        await env.client.call(`${L}/acceptTermsOfUse`, { useBackgroundDataCollection: false });
        const reply = await env.client.call(`${L}/getCurrentPosition`, { accuracy: 3 });
        assert.equal(reply.latitude, 3);
        assert.deepEqual(env.lookups, ["wifi"]);
    });

    test("no source finds anything: position unavailable", async () => {
        env.gps = undefined;
        await assert.rejects(env.client.call(`${L}/getCurrentPosition`, {}), failsWith(ERRORS.positionUnavailable));
    });

    test("getLocationServicePrefs has every field the Location Services app reads", async () => {
        await env.client.call(`${L}/setAutoLocate`, { autoLocate: true });
        const reply = await env.client.call(`${L}/getLocationServicePrefs`, {});
        assert.deepEqual(reply, {
            returnValue: true, autoLocate: true, geotagPhotos: false, isTermsOfUseAccepted: false,
            useBackgroundDataCollection: false, webSetting: true, useGoogle: false, useGps: true,
            gpsAvailable: true, cameraAvailable: false,
        });
        assert.equal(JSON.parse(env.stored!).autoLocate, true, "kept");
        await assert.rejects(env.client.call(`${L}/setAutoLocate`, { autoLocate: "yes" }), failsWith(-1));
    });

    test("a getter subscription hears each change, as the camera's getGeotagPhotos does", async () => {
        const replies: Payload[] = [];
        const stop = new AbortController();
        const watching = (async () => {
            for await (const reply of env.client.subscribe(`${L}/getGeotagPhotos`, { subscribe: true }, { signal: stop.signal })) {
                replies.push(reply);
            }
        })();
        await waitFor(() => replies.length === 1);
        await env.client.call(`${L}/setGeotagPhotos`, { geotagPhotos: true });
        await waitFor(() => replies.length === 2);
        stop.abort();
        await watching;
        assert.equal(replies[0]!.geotagPhotos, false);
        assert.equal(replies[1]!.geotagPhotos, true);
    });

    test("a website is asked about through the system UI, and the alert's answer is the reply", async () => {
        const asking = env.client.call(`${L}/getCurrentPosition`, { url: "https://maps.example.org/place/1" });
        await waitFor(() => env.published.length === 1);
        assert.deepEqual(env.published[0], {
            event: "registerForLocationServiceNotifications",
            message: { web: { url: "https://maps.example.org", name: "maps.example.org" } },
        });
        // The alert passes back what it was opened with.
        await env.client.call(`${L}/acceptAlwaysLocationRequest`, { url: "https://maps.example.org" });
        assert.equal((await asking).latitude, 1);
        await env.client.call(`${L}/getCurrentPosition`, { url: "https://maps.example.org/other" });
        assert.equal(env.published.length, 1, "always allowed");

        const refused = env.client.call(`${L}/getCurrentPosition`, { url: "https://other.example.org/" });
        await waitFor(() => env.published.length === 2);
        await env.client.call(`${L}/rejectLocationRequest`, { url: "https://other.example.org" });
        await assert.rejects(refused, failsWith(ERRORS.denied));
    });

    test("Never Share Location refuses websites without asking", async () => {
        await env.client.call(`${L}/setWebSetting`, { webSetting: false });
        await assert.rejects(env.client.call(`${L}/getCurrentPosition`, { url: "https://a.example.org/" }),
            failsWith(ERRORS.denied));
        assert.equal(env.published.length, 0);
    });

    test("startTracking sends a position, then each new one", async () => {
        const replies: Payload[] = [];
        const stop = new AbortController();
        const tracking = (async () => {
            for await (const reply of env.client.subscribe(`${L}/startTracking`, { subscribe: true }, { signal: stop.signal })) {
                replies.push(reply);
                if (replies.length === 1) {
                    env.gps = fixAt(9, 9, 5);
                }
            }
        })();
        await waitFor(() => replies.length === 2);
        stop.abort();
        await tracking;
        assert.equal(replies[0]!.latitude, 1);
        assert.equal(replies[1]!.latitude, 9);
    });

    test("getReverseLocation needs the terms, like HP's Google lookup", async () => {
        await assert.rejects(env.client.call(`${L}/getReverseLocation`, { latitude: 1, longitude: 2 }),
            failsWith(ERRORS.termsNotAccepted));
        await env.client.call(`${L}/acceptTermsOfUse`, {});
        const reply = await env.client.call(`${L}/getReverseLocation`, { latitude: 1, longitude: 2 });
        assert.equal(reply.address, "1;2");
        assert.equal(reply.errorCode, 0);
        await assert.rejects(env.client.call(`${L}/getReverseLocation`, {}), failsWith(-1));
    });
});
