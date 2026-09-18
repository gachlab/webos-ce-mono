// com.palm.location's parts: its sources, the choice between them, the
// preferences and the user's consent.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { isLunaError, type Payload } from "#kit/luna.ts";
import { locationCommands } from "../services/com.palm.location/commands.ts";
import { askerOf, createConsent, siteOf, type Consent } from "../services/com.palm.location/consent.ts";
import { addressFromNominatim } from "../services/com.palm.location/geocode.ts";
import { allowedSources, createLocator, ERRORS, REMOTE_REFRESH_MS, RESPONSE_MS } from "../services/com.palm.location/locator.ts";
import { createPrefs, DEFAULT_PREFS, type Prefs, type PrefsStore } from "../services/com.palm.location/prefs.ts";
import {
    BEACONDB_URL, createGpsSource, createIpSource, createWifiSource, fixFromTpv, parseNmcliScan,
    UNKNOWN, type Fix, type Source, type SourceName,
} from "../services/com.palm.location/sources.ts";

const fix = (latitude: number, timestamp = 1000): Fix => ({
    latitude, longitude: -latitude, horizAccuracy: 10, vertAccuracy: UNKNOWN, altitude: UNKNOWN,
    heading: UNKNOWN, velocity: UNKNOWN, timestamp,
});

const memoryPrefs = (initial: Partial<Prefs> = {}): PrefsStore & { written: string[] } => {
    const written: string[] = [];
    const store = createPrefs({ read: () => JSON.stringify({ ...DEFAULT_PREFS, ...initial }), write: (t) => written.push(t) });
    return Object.assign(store, { written });
};

const failsWith = (errorCode: number) => (error: unknown) => {
    assert.ok(isLunaError(error), String(error));
    assert.equal((error as unknown as Payload).errorCode, errorCode);
    return true;
};

const json = (status: number, body: unknown) =>
    new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json" } });

describe("sources", () => {
    test("a gpsd report is a fix only with a 2D fix or better", () => {
        assert.equal(fixFromTpv({ class: "TPV", mode: 1, lat: 1, lon: 2 }, 5), undefined);
        assert.equal(fixFromTpv({ class: "SKY", mode: 3, lat: 1, lon: 2 }, 5), undefined);
        const flat = fixFromTpv({ class: "TPV", mode: 2, lat: 1, lon: 2, eph: 12, alt: 100, epv: 3 }, 5)!;
        assert.equal(flat.horizAccuracy, 12);
        assert.equal(flat.altitude, UNKNOWN, "a 2D fix has no altitude");
        assert.equal(flat.timestamp, 5);
        const full = fixFromTpv({
            class: "TPV", mode: 3, lat: 1, lon: 2, epx: 4, epy: 7, altHAE: 30, epv: 9, track: 90, speed: 2,
            time: "2026-09-17T10:00:00.000Z",
        }, 5)!;
        assert.deepEqual(full, {
            latitude: 1, longitude: 2, horizAccuracy: 7, vertAccuracy: 9, altitude: 30, heading: 90, velocity: 2,
            timestamp: Date.parse("2026-09-17T10:00:00.000Z"),
        });
    });

    test("the GPS source waits for the first fix and then lets gpsd go", async () => {
        let stopped = false;
        const gps = createGpsSource({
            now: () => 5,
            lines: async function* (signal) {
                signal.addEventListener("abort", () => { stopped = true; });
                yield '{"class":"VERSION"}';
                yield "not json";
                yield '{"class":"TPV","mode":1}';
                yield '{"class":"TPV","mode":2,"lat":10,"lon":20,"eph":8}';
                yield '{"class":"TPV","mode":3,"lat":11,"lon":21}';
            },
        });
        const found = await gps.locate(new AbortController().signal);
        assert.equal(found?.latitude, 10);
        assert.equal(stopped, true);
        assert.equal(gps.remote, false);
    });

    test("nmcli's scan becomes access points, without _nomap networks", () => {
        const text = [
            "1C\\:61\\:B4\\:F7\\:FE\\:F3:100:2422 MHz:Home",
            "8A\\:61\\:B4\\:EC\\:FC\\:BC:50:5200 MHz:Name\\:with colon",
            "AA\\:BB\\:CC\\:DD\\:EE\\:FF:70:2412 MHz:Phone_nomap",
            "garbage",
            "",
        ].join("\n");
        assert.deepEqual(parseNmcliScan(text), [
            { macAddress: "1c:61:b4:f7:fe:f3", signalStrength: -50, frequency: 2422 },
            { macAddress: "8a:61:b4:ec:fc:bc", signalStrength: -75, frequency: 5200 },
        ]);
    });

    test("the Wi-Fi source asks BeaconDB with at least two access points, and only with them", async () => {
        const asked: Payload[] = [];
        const fetch = async (url: string, init: RequestInit) => {
            assert.equal(url, BEACONDB_URL);
            asked.push(JSON.parse(String(init.body)));
            return json(200, { location: { lat: 43.1, lng: -77.6 }, accuracy: 30 });
        };
        const one = [{ macAddress: "aa:aa:aa:aa:aa:aa", signalStrength: -50, frequency: 2412 }];
        const lonely = createWifiSource({ scan: async () => one, fetch, now: () => 7 });
        assert.equal(await lonely.locate(new AbortController().signal), undefined);
        assert.equal(asked.length, 0, "a single access point is never sent");

        const two = [...one, { macAddress: "bb:bb:bb:bb:bb:bb", signalStrength: -60, frequency: 5200 }];
        const wifi = createWifiSource({ scan: async () => two, fetch, now: () => 7 });
        const found = await wifi.locate(new AbortController().signal);
        assert.deepEqual(asked, [{ wifiAccessPoints: two, considerIp: false }]);
        assert.equal(found?.latitude, 43.1);
        assert.equal(found?.horizAccuracy, 30);
        assert.equal(found?.timestamp, 7);
        assert.equal(wifi.remote, true);
    });

    test("BeaconDB's 404 is no position, and another failure is an error", async () => {
        const unknown = createIpSource({ fetch: async () => json(404, { error: {} }), now: () => 1 });
        assert.equal(await unknown.locate(new AbortController().signal), undefined);
        const broken = createIpSource({ fetch: async () => json(500, {}), now: () => 1 });
        await assert.rejects(broken.locate(new AbortController().signal), /500/);
        const ip = createIpSource({
            fetch: async (_url, init) => {
                assert.deepEqual(JSON.parse(String(init.body)), { considerIp: true });
                return json(200, { location: { lat: 1, lng: 2 }, accuracy: 25000, fallback: "ipf" });
            },
            now: () => 1,
        });
        assert.equal((await ip.locate(new AbortController().signal))?.horizAccuracy, 25000);
    });
});

const source = (name: SourceName, answer: () => Promise<Fix | undefined>, calls: string[]): Source => ({
    name,
    remote: name !== "gps",
    locate: () => {
        calls.push(name);
        return answer();
    },
});

const ALL_ON: Partial<Prefs> = { useGps: true, useGoogle: true, isTermsOfUseAccepted: true };

describe("locator", () => {
    const three = (calls: string[], answers: Partial<Record<SourceName, () => Promise<Fix | undefined>>> = {}) =>
        (["gps", "wifi", "ip"] as const).map((name) => source(name, answers[name] ?? (async () => undefined), calls));

    test("the network sources wait for the terms, and GPS for its switch", () => {
        const sources = three([]);
        const names = (prefs: Partial<Prefs>, accuracy: 1 | 2 | 3) =>
            allowedSources(sources, { ...DEFAULT_PREFS, ...prefs }, accuracy).map((s) => s.name);
        assert.deepEqual(names({}, 1), ["gps"]);
        assert.deepEqual(names({ useGoogle: true }, 1), ["gps"], "not before the terms");
        assert.deepEqual(names(ALL_ON, 1), ["gps", "wifi", "ip"]);
        assert.deepEqual(names(ALL_ON, 2), ["wifi", "gps", "ip"]);
        assert.deepEqual(names(ALL_ON, 3), ["wifi", "ip", "gps"]);
        assert.deepEqual(names({ ...ALL_ON, useGps: false }, 1), ["wifi", "ip"]);
    });

    test("the first source with a position answers; the ones after are not asked", async () => {
        const calls: string[] = [];
        const logs: string[] = [];
        const locator = createLocator({
            sources: three(calls, { gps: async () => { throw new Error("gpsd is gone"); }, wifi: async () => fix(5) }),
            prefs: () => ({ ...DEFAULT_PREFS, ...ALL_ON }), now: () => 1000, log: (m) => logs.push(m),
        });
        const found = await locator.locate({ accuracy: 1, responseTime: 1, maximumAge: 0 }, new AbortController().signal);
        assert.equal(found.latitude, 5);
        assert.deepEqual(calls, ["gps", "wifi"]);
        assert.deepEqual(logs, ["gps: gpsd is gone"]);
    });

    test("nothing switched on, nothing accepted, nothing found: HP's error codes", async () => {
        const request = { accuracy: 3, responseTime: 1, maximumAge: 0 } as const;
        const signal = new AbortController().signal;
        const off = createLocator({ sources: three([]), prefs: () => ({ ...DEFAULT_PREFS, useGps: false }), now: () => 0, log: () => {} });
        await assert.rejects(off.locate(request, signal), failsWith(ERRORS.serviceOff));
        const noTerms = createLocator({
            sources: three([]), prefs: () => ({ ...DEFAULT_PREFS, useGps: false, useGoogle: true }), now: () => 0, log: () => {},
        });
        await assert.rejects(noTerms.locate(request, signal), failsWith(ERRORS.termsNotAccepted));
        const calls: string[] = [];
        const none = createLocator({ sources: three(calls), prefs: () => ({ ...DEFAULT_PREFS, ...ALL_ON }), now: () => 0, log: () => {} });
        await assert.rejects(none.locate(request, signal), failsWith(ERRORS.positionUnavailable));
        assert.deepEqual(calls, ["wifi", "ip", "gps"]);
    });

    test("a source that never answers ends in a timeout at the request's response time", async () => {
        const deadlines: number[] = [];
        const expire = new AbortController();
        const hanging = (_name: SourceName): Source => ({
            name: "gps",
            remote: false,
            locate: (signal) => new Promise((_resolve, reject) => {
                signal.addEventListener("abort", () => reject(signal.reason), { once: true });
            }),
        });
        const locator = createLocator({
            sources: [hanging("gps")],
            prefs: () => DEFAULT_PREFS, now: () => 0, log: () => {},
            deadline: (ms) => {
                deadlines.push(ms);
                return expire.signal;
            },
        });
        const pending = locator.locate({ accuracy: 1, responseTime: 2, maximumAge: 0 }, new AbortController().signal);
        expire.abort(new Error("time"));
        await assert.rejects(pending, failsWith(ERRORS.timeout));
        assert.deepEqual(deadlines, [RESPONSE_MS[2]]);
        assert.deepEqual(RESPONSE_MS, { 1: 10_000, 2: 30_000, 3: 60_000 });
    });

    test("a network source is asked again only after a minute, even for no position", async () => {
        const calls: string[] = [];
        let clock = 0;
        let wifiKnows = false;
        const locator = createLocator({
            sources: three(calls, {
                wifi: async () => (wifiKnows ? fix(7, clock) : undefined),
                ip: async () => fix(8, clock),
            }),
            prefs: () => ({ ...DEFAULT_PREFS, ...ALL_ON, useGps: false }), now: () => clock, log: () => {},
        });
        const request = { accuracy: 3, responseTime: 1, maximumAge: 0 } as const;
        const signal = new AbortController().signal;
        assert.equal((await locator.locate(request, signal)).latitude, 8);
        wifiKnows = true;
        clock += 10_000;
        assert.equal((await locator.locate(request, signal)).latitude, 8, "the answers of ten seconds ago");
        assert.deepEqual(calls, ["wifi", "ip"]);
        clock += REMOTE_REFRESH_MS;
        assert.equal((await locator.locate(request, signal)).latitude, 7);
        assert.deepEqual(calls, ["wifi", "ip", "wifi"]);
        assert.equal(REMOTE_REFRESH_MS, 60_000);
    });

    test("a recent enough position is answered without looking again", async () => {
        const calls: string[] = [];
        let clock = 10_000;
        const locator = createLocator({
            sources: three(calls, { gps: async () => fix(clock, clock) }),
            prefs: () => ({ ...DEFAULT_PREFS, ...ALL_ON }), now: () => clock, log: () => {},
        });
        const signal = new AbortController().signal;
        await locator.locate({ accuracy: 1, responseTime: 1, maximumAge: 0 }, signal);
        clock += 30_000;
        const cached = await locator.locate({ accuracy: 1, responseTime: 1, maximumAge: 60 }, signal);
        assert.equal(cached.latitude, 10_000);
        assert.deepEqual(calls, ["gps"]);
        const fresh = await locator.locate({ accuracy: 1, responseTime: 1, maximumAge: 10 }, signal);
        assert.equal(fresh.latitude, 40_000);
        assert.deepEqual(calls, ["gps", "gps"]);
    });
});

describe("preferences", () => {
    test("nothing leaves the device by default", () => {
        const prefs = createPrefs({ read: () => undefined, write: () => {} }).get();
        assert.equal(prefs.useGoogle, false);
        assert.equal(prefs.isTermsOfUseAccepted, false);
        assert.equal(prefs.autoLocate, false);
        assert.equal(prefs.webSetting, true);
    });

    test("a stored value of the wrong type is ignored, and a change is written and announced", () => {
        const prefs = memoryPrefs({ useGps: "yes" as unknown as boolean, autoLocate: true });
        assert.equal(prefs.get().useGps, true);
        assert.equal(prefs.get().autoLocate, true);
        const seen: boolean[] = [];
        const stop = prefs.watch((p) => seen.push(p.useGps));
        prefs.set({ useGps: false });
        stop();
        prefs.set({ useGps: true });
        assert.deepEqual(seen, [false]);
        assert.equal(JSON.parse(prefs.written[0]!).useGps, false);
        assert.equal(createPrefs({ read: () => "{broken", write: () => {} }).get().useGps, true);
    });
});

describe("consent", () => {
    const setup = (initial: Partial<Prefs> = {}, publishFails = false) => {
        const published: Payload[] = [];
        const prefs = memoryPrefs(initial);
        const consent: Consent = createConsent({
            prefs,
            publish: async (payload) => {
                if (publishFails) {
                    throw new Error("no system UI");
                }
                published.push(payload);
            },
            log: () => {},
        });
        return { consent, published, prefs };
    };
    const tick = () => new Promise((resolve) => setImmediate(resolve));
    const signal = () => new AbortController().signal;

    test("who asks: the hub's application id, a url only from the system, the shell as the system", () => {
        assert.deepEqual(askerOf("com.example.app 1234", {}), { kind: "app", appId: "com.example.app" });
        assert.deepEqual(askerOf("com.example.app", { url: "https://evil.example/" }), { kind: "app", appId: "com.example.app" });
        assert.deepEqual(askerOf("com.palm.launcher", { appId: "com.palm.launcher" }), { kind: "system" });
        assert.deepEqual(askerOf(undefined, { url: "https://maps.example.org/a/b?c" }),
            { kind: "web", url: "https://maps.example.org", name: "maps.example.org" });
        assert.deepEqual(askerOf(undefined, { appId: "com.example.app" }), { kind: "system" });
        assert.equal(siteOf("not a url"), "not a url");
    });

    test("the system is never asked", async () => {
        const { consent, published } = setup();
        await consent.check({ kind: "system" }, signal());
        assert.equal(published.length, 0);
    });

    test("an application is asked once a session, as the system UI expects", async () => {
        const { consent, published } = setup();
        let allowed = false;
        const pending = consent.check({ kind: "app", appId: "com.example.a" }, signal()).then(() => { allowed = true; });
        await tick();
        assert.deepEqual(published, [{ event: "registerForLocationServiceNotifications", message: { appId: "com.example.a" } }]);
        assert.equal(allowed, false);
        consent.answer("accept", "com.example.a");
        await pending;
        await consent.check({ kind: "app", appId: "com.example.a" }, signal());
        assert.equal(published.length, 1, "not asked again this session");
    });

    test("Auto Locate lets applications through without asking", async () => {
        const { consent, published } = setup({ autoLocate: true });
        await consent.check({ kind: "app", appId: "com.example.a" }, signal());
        assert.equal(published.length, 0);
    });

    test("Don't Allow and a closed alert refuse; only Don't Allow is remembered", async () => {
        const { consent, published } = setup();
        const first = consent.check({ kind: "app", appId: "com.example.a" }, signal());
        await tick();
        consent.answer("ignore", "com.example.a");
        await assert.rejects(first, failsWith(ERRORS.denied));
        const second = consent.check({ kind: "app", appId: "com.example.a" }, signal());
        await tick();
        assert.equal(published.length, 2, "a closed alert asks again next time");
        consent.answer("reject", "com.example.a");
        await assert.rejects(second, failsWith(ERRORS.denied));
        await assert.rejects(consent.check({ kind: "app", appId: "com.example.a" }, signal()), failsWith(ERRORS.denied));
        assert.equal(published.length, 2, "a refusal holds for the session");
    });

    test("questions wait their turn, and requests for the same one share it", async () => {
        const { consent, published } = setup();
        const results: string[] = [];
        const a1 = consent.check({ kind: "app", appId: "a" }, signal()).then(() => results.push("a1"));
        const b = consent.check({ kind: "app", appId: "b" }, signal()).then(() => results.push("b"), () => results.push("b refused"));
        const a2 = consent.check({ kind: "app", appId: "a" }, signal()).then(() => results.push("a2"));
        await tick();
        assert.equal(published.length, 1, "one alert at a time");
        consent.answer("reject", "b");
        await b;
        assert.equal(published.length, 1, "an answer for a question not asked yet does not open another");
        consent.answer("accept", "a");
        await Promise.all([a1, a2]);
        assert.deepEqual(results, ["b refused", "a1", "a2"]);
    });

    test("a request that goes away stops waiting, and the question stays for the others", async () => {
        const { consent } = setup();
        const leaving = new AbortController();
        const gone = consent.check({ kind: "app", appId: "a" }, leaving.signal);
        const staying = consent.check({ kind: "app", appId: "a" }, signal());
        await tick();
        leaving.abort(new Error("cancelled"));
        await assert.rejects(gone, /cancelled/);
        consent.answer("accept", "a");
        await staying;
    });

    test("an alert nobody answers counts as ignored, and the next question is asked", async () => {
        const published: Payload[] = [];
        const timers: { callback: () => void; ms: number; cancelled: boolean }[] = [];
        const consent = createConsent({
            prefs: memoryPrefs(),
            publish: async (payload) => { published.push(payload); },
            log: () => {},
            later: (callback, ms) => {
                const timer = { callback, ms, cancelled: false };
                timers.push(timer);
                return { cancel: () => { timer.cancelled = true; } };
            },
        });
        const a = consent.check({ kind: "app", appId: "a" }, signal());
        const b = consent.check({ kind: "app", appId: "b" }, signal());
        await tick();
        assert.equal(published.length, 1);
        assert.equal(timers[0]!.ms, 60_000);
        timers[0]!.callback();
        await assert.rejects(a, failsWith(ERRORS.denied));
        await tick();
        assert.equal(published.length, 2, "b is asked once a is given up on");
        consent.answer("accept", "b");
        await b;
        assert.equal(timers[1]!.cancelled, true, "an answered alert stops its clock");
    });

    test("without a system UI to ask, the answer is no", async () => {
        const { consent } = setup({}, true);
        await assert.rejects(consent.check({ kind: "app", appId: "a" }, signal()), failsWith(ERRORS.denied));
    });

    test("websites: Never Share, Always Allow, and Clear My Location Data", async () => {
        const site = { kind: "web", url: "https://maps.example.org", name: "maps.example.org" } as const;
        const never = setup({ webSetting: false });
        await assert.rejects(never.consent.check(site, signal()), failsWith(ERRORS.denied));
        assert.equal(never.published.length, 0);

        const { consent, published, prefs } = setup();
        const asked = consent.check(site, signal());
        await tick();
        assert.deepEqual(published[0], {
            event: "registerForLocationServiceNotifications",
            message: { web: { url: "https://maps.example.org", name: "maps.example.org" } },
        });
        consent.answer("acceptAlways", "https://maps.example.org");
        await asked;
        assert.deepEqual(prefs.get().allowedSites, ["https://maps.example.org"]);

        // Remembered past this session.
        const later = createConsent({ prefs, publish: async (p) => { published.push(p); }, log: () => {} });
        await later.check(site, signal());
        assert.equal(published.length, 1);

        later.clearSites();
        assert.deepEqual(prefs.get().allowedSites, []);
        const again = later.check(site, signal());
        await tick();
        assert.equal(published.length, 2);
        later.answer("accept", "https://maps.example.org");
        await again;
    });
});

describe("commands", () => {
    test("a preference subscriber hears a change made while its last reply was on its way", async () => {
        const prefs = memoryPrefs();
        const commands = locationCommands({
            locator: { locate: async () => fix(0) },
            consent: createConsent({ prefs, publish: async () => {}, log: () => {} }),
            prefs,
            reverse: async () => undefined,
            gpsAvailable: async () => false,
            cameraAvailable: async () => false,
            sleep: async () => {},
        });
        const getter = commands.find((c) => c.name === "getGeotagPhotos")!;
        const stop = new AbortController();
        const replies = getter.handler({
            method: "getGeotagPhotos", category: "/", payload: { subscribe: true }, subscribe: true,
            sender: undefined, senderServiceName: undefined, applicationId: undefined, signal: stop.signal,
        }) as AsyncGenerator<Payload>;
        assert.equal((await replies.next()).value.geotagPhotos, false);
        const second = replies.next();
        prefs.set({ geotagPhotos: true });
        assert.equal((await second).value.geotagPhotos, true);
        // Changed again before anyone asked for the next reply.
        prefs.set({ geotagPhotos: false });
        const third = await Promise.race([replies.next(), new Promise((resolve) => setTimeout(resolve, 200, "lost"))]);
        assert.notEqual(third, "lost", "the change was lost");
        assert.equal((third as IteratorResult<Payload>).value.geotagPhotos, false);
        stop.abort();
        await replies.return(undefined);
    });
});

describe("reverse geocoding", () => {
    test("Nominatim's address in HP's fields", () => {
        assert.deepEqual(addressFromNominatim({
            address: { house_number: "10", road: "Main St", suburb: "Downtown", city: "Rochester", state: "NY", postcode: "14604", country: "United States" },
        }), {
            address: "10 Main St;Rochester, NY 14604;United States",
            street: "10 Main St",
            substreet: "Downtown",
            zipcode: "14604",
            country: "United States",
        });
        assert.equal(addressFromNominatim({}), undefined);
        assert.equal(addressFromNominatim({ address: {} }), undefined);
    });
});
