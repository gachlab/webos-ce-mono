// com.palm.location's commands, with HP's names, parameters and replies.
//
// Left out: cellChange and wifiChange, the activity callbacks HP's service
// gave itself to learn of a new cell or network; nothing else calls them.

import { lunaError, type Payload, type Request } from "#kit/luna.ts";
import type { Command } from "#kit/mojoservice.ts";
import { askerOf, type Answer, type Consent } from "./consent.ts";
import { ERRORS, level, positionReply, type Locator } from "./locator.ts";
import type { Prefs, PrefsStore } from "./prefs.ts";
import type { Fix } from "./sources.ts";

export interface CommandDeps {
    readonly locator: Locator;
    readonly consent: Consent;
    readonly prefs: PrefsStore;
    // An address for a position, or undefined when there is none.
    readonly reverse: (latitude: number, longitude: number, signal: AbortSignal) => Promise<Payload | undefined>;
    readonly gpsAvailable: () => Promise<boolean>;
    readonly cameraAvailable: () => Promise<boolean>;
    readonly sleep: (ms: number, signal: AbortSignal) => Promise<void>;
    // How often startTracking looks again.
    readonly trackMs?: number;
}

// Longer than the slowest request waits (RESPONSE_MS[3]), so the caller hears
// the service's own timeout.
const LOCATE_TIMEOUT = 70;

export const TRACK_MS = 10_000;

const asBoolean = (payload: Payload, key: string): boolean => {
    const value = payload[key];
    if (typeof value !== "boolean") {
        throw lunaError(-1, `${key} must be a boolean`);
    }
    return value;
};

const same = (a: Fix, b: Fix) =>
    a.latitude === b.latitude && a.longitude === b.longitude && a.horizAccuracy === b.horizAccuracy;

// A getter: the value now and, for a subscriber, again after each change.
const getter = (deps: CommandDeps, pick: (prefs: Prefs) => Payload) =>
    async function* ({ subscribe, signal }: Request): AsyncGenerator<Payload> {
        let current = pick(deps.prefs.get());
        yield { ...current, ...(subscribe ? { subscribed: true } : {}) };
        if (!subscribe) {
            return;
        }
        let wake: (() => void) | undefined;
        const stop = deps.prefs.watch(() => wake?.());
        const aborted = () => wake?.();
        signal.addEventListener("abort", aborted, { once: true });
        try {
            while (!signal.aborted) {
                await new Promise<void>((resolve) => { wake = resolve; });
                const next = pick(deps.prefs.get());
                if (!signal.aborted && JSON.stringify(next) !== JSON.stringify(current)) {
                    current = next;
                    yield next;
                }
            }
        } finally {
            stop();
            signal.removeEventListener("abort", aborted);
        }
    };

const setter = (deps: CommandDeps, key: keyof Prefs) =>
    ({ payload }: Request) => {
        deps.prefs.set({ [key]: asBoolean(payload, key) });
        return {};
    };

const answer = (deps: CommandDeps, kind: Answer) =>
    ({ payload }: Request) => {
        const key = typeof payload.appId === "string" ? payload.appId
            : typeof payload.url === "string" ? payload.url : undefined;
        if (!key) {
            throw lunaError(-1, "appId or url is required");
        }
        deps.consent.answer(kind, key);
        return {};
    };

const locateRequest = (payload: Payload, accuracy: 1 | 2 | 3, responseTime: 1 | 2 | 3) => ({
    accuracy: level(payload.accuracy, accuracy),
    responseTime: level(payload.responseTime, responseTime),
    maximumAge: typeof payload.maximumAge === "number" && payload.maximumAge > 0 ? payload.maximumAge : 0,
});

export const locationCommands = (deps: CommandDeps): Command[] => [
    {
        name: "getCurrentPosition",
        public: true,
        timeout: LOCATE_TIMEOUT,
        handler: async ({ payload, applicationId, signal }) => {
            await deps.consent.check(askerOf(applicationId, payload), signal);
            return positionReply(await deps.locator.locate(locateRequest(payload, 2, 2), signal));
        },
    },
    {
        name: "startTracking",
        public: true,
        timeout: LOCATE_TIMEOUT,
        // A position as soon as there is one, then each time it changes. A
        // position that cannot be had later on is skipped, not the end of it.
        handler: async function* ({ payload, applicationId, signal }): AsyncGenerator<Payload> {
            await deps.consent.check(askerOf(applicationId, payload), signal);
            const request = { ...locateRequest(payload, 1, 3), maximumAge: 0 };
            let last = await deps.locator.locate(request, signal);
            yield positionReply(last);
            while (!signal.aborted) {
                await deps.sleep(deps.trackMs ?? TRACK_MS, signal).catch(() => undefined);
                if (signal.aborted) {
                    break;
                }
                try {
                    const fix = await deps.locator.locate(request, signal);
                    if (!same(fix, last)) {
                        last = fix;
                        yield positionReply(fix);
                    }
                } catch {
                    // Looked for again next time.
                }
            }
        },
    },
    {
        name: "getReverseLocation",
        public: true,
        timeout: LOCATE_TIMEOUT,
        handler: async ({ payload, applicationId, signal }) => {
            const { latitude, longitude } = payload;
            if (typeof latitude !== "number" || typeof longitude !== "number") {
                throw lunaError(-1, "latitude and longitude are required");
            }
            await deps.consent.check(askerOf(applicationId, payload), signal);
            // The address comes from a server, as HP's came from Google's.
            const prefs = deps.prefs.get();
            if (!prefs.useGoogle || !prefs.isTermsOfUseAccepted) {
                throw lunaError(ERRORS.termsNotAccepted, "The location terms of use were not accepted");
            }
            const address = await deps.reverse(latitude, longitude, signal);
            if (!address) {
                throw lunaError(ERRORS.positionUnavailable, "No address for this position");
            }
            return { errorCode: 0, ...address };
        },
    },

    { name: "getLocationServicePrefs", public: true, handler: async () => {
        const prefs = deps.prefs.get();
        const [gpsAvailable, cameraAvailable] = await Promise.all([deps.gpsAvailable(), deps.cameraAvailable()]);
        return {
            autoLocate: prefs.autoLocate,
            geotagPhotos: prefs.geotagPhotos,
            isTermsOfUseAccepted: prefs.isTermsOfUseAccepted,
            useBackgroundDataCollection: prefs.useBackgroundDataCollection,
            webSetting: prefs.webSetting,
            useGoogle: prefs.useGoogle,
            useGps: prefs.useGps,
            gpsAvailable,
            cameraAvailable,
        };
    } },
    {
        name: "setLocationServicePrefs",
        // The Location Services app's master switch: off stops every source.
        handler: ({ payload }) => {
            const change: { -readonly [K in keyof Prefs]?: Prefs[K] } = {};
            if (payload.useLocationServices !== undefined) {
                const on = asBoolean(payload, "useLocationServices");
                Object.assign(change, on ? { useGps: true, useGoogle: deps.prefs.get().isTermsOfUseAccepted }
                    : { useGps: false, useGoogle: false });
            }
            if (payload.useBackgroundDataCollection !== undefined) {
                change.useBackgroundDataCollection = asBoolean(payload, "useBackgroundDataCollection");
            }
            deps.prefs.set(change);
            return {};
        },
    },
    { name: "getUseGps", public: true, handler: getter(deps, (p) => ({ useGps: p.useGps })) },
    { name: "setUseGps", handler: setter(deps, "useGps") },
    { name: "setUseGoogle", handler: setter(deps, "useGoogle") },
    { name: "getAutoLocate", public: true, handler: getter(deps, (p) => ({ autoLocate: p.autoLocate })) },
    { name: "setAutoLocate", handler: setter(deps, "autoLocate") },
    { name: "getWebSetting", public: true, handler: getter(deps, (p) => ({ webSetting: p.webSetting })) },
    { name: "setWebSetting", handler: setter(deps, "webSetting") },
    { name: "clearWebSetting", handler: () => {
        deps.consent.clearSites();
        return {};
    } },
    { name: "getGeotagPhotos", public: true, handler: getter(deps, (p) => ({ geotagPhotos: p.geotagPhotos })) },
    { name: "setGeotagPhotos", handler: setter(deps, "geotagPhotos") },
    {
        name: "getUseBackgroundDataCollection",
        public: true,
        handler: getter(deps, (p) => ({ useBackgroundDataCollection: p.useBackgroundDataCollection })),
    },
    { name: "setUseBackgroundDataCollection", handler: setter(deps, "useBackgroundDataCollection") },

    {
        name: "acceptTermsOfUse",
        handler: ({ payload }) => {
            deps.prefs.set({
                isTermsOfUseAccepted: true,
                useGoogle: true,
                ...(typeof payload.useBackgroundDataCollection === "boolean"
                    ? { useBackgroundDataCollection: payload.useBackgroundDataCollection } : {}),
            });
            return {};
        },
    },
    {
        name: "rejectTermsOfUse",
        handler: () => {
            deps.prefs.set({ isTermsOfUseAccepted: false, useGoogle: false, useBackgroundDataCollection: false });
            return {};
        },
    },
    { name: "acceptLocationRequest", handler: answer(deps, "accept") },
    { name: "acceptAlwaysLocationRequest", handler: answer(deps, "acceptAlways") },
    { name: "rejectLocationRequest", handler: answer(deps, "reject") },
    { name: "ignoreLocationRequest", handler: answer(deps, "ignore") },
];
