// Finding a position: which sources a request may use, in which order, for how
// long, and what it is told when none of them can answer.

import { lunaError } from "#kit/luna.ts";
import type { Prefs } from "./prefs.ts";
import type { Fix, Source, SourceName } from "./sources.ts";

// HP's error codes, as its callers read them (Maps, the Location Services app,
// the Palm SDK's documentation).
export const ERRORS = {
    timeout: 1,
    positionUnavailable: 2,
    // No source is switched on.
    serviceOff: 5,
    // The only sources left need the terms, which were not accepted.
    termsNotAccepted: 6,
    // The user did not allow this application or website.
    denied: 8,
} as const;

// HP's request levels: 1 is the finest and the slowest to wait for.
export type Level = 1 | 2 | 3;

export const level = (value: unknown, fallback: Level): Level =>
    value === 1 || value === 2 || value === 3 ? value : fallback;

// Which source to ask first. A fine position is worth waiting for a GPS; a
// coarse one should come back fast, and the network sources are fast.
const ORDER: Record<Level, readonly SourceName[]> = {
    1: ["gps", "wifi", "ip"],
    2: ["wifi", "gps", "ip"],
    3: ["wifi", "ip", "gps"],
};

// How long a request waits, by its responseTime level.
export const RESPONSE_MS: Record<Level, number> = { 1: 10_000, 2: 30_000, 3: 60_000 };

export interface LocateRequest {
    readonly accuracy: Level;
    readonly responseTime: Level;
    // Seconds; a position this recent is answered without looking again.
    readonly maximumAge: number;
}

export interface Locator {
    locate(request: LocateRequest, signal: AbortSignal): Promise<Fix>;
}

export interface LocatorDeps {
    readonly sources: readonly Source[];
    readonly prefs: () => Prefs;
    readonly now: () => number;
    readonly log: (message: string) => void;
    // A signal aborted after `ms`; AbortSignal.timeout when left out.
    readonly deadline?: (ms: number) => AbortSignal;
}

// The sources the preferences allow, in the order the request wants them.
export const allowedSources = (sources: readonly Source[], prefs: Prefs, accuracy: Level): Source[] =>
    ORDER[accuracy]
        .map((name) => sources.find((source) => source.name === name))
        .filter((source): source is Source => source !== undefined)
        .filter((source) => (source.remote ? prefs.useGoogle && prefs.isTermsOfUseAccepted : prefs.useGps));

export const createLocator = (deps: LocatorDeps): Locator => {
    let last: Fix | undefined;
    return {
        locate: async (request, signal) => {
            const prefs = deps.prefs();
            const sources = allowedSources(deps.sources, prefs, request.accuracy);
            if (sources.length === 0) {
                if (prefs.useGoogle && !prefs.isTermsOfUseAccepted) {
                    throw lunaError(ERRORS.termsNotAccepted, "The location terms of use were not accepted");
                }
                throw lunaError(ERRORS.serviceOff, "Location services are off");
            }
            if (last && request.maximumAge > 0 && deps.now() - last.timestamp <= request.maximumAge * 1000) {
                return last;
            }

            const deadline = (deps.deadline ?? AbortSignal.timeout)(RESPONSE_MS[request.responseTime]);
            const stop = AbortSignal.any([signal, deadline]);
            for (const source of sources) {
                if (stop.aborted) {
                    break;
                }
                try {
                    const fix = await source.locate(stop);
                    if (fix) {
                        last = fix;
                        return fix;
                    }
                } catch (error) {
                    if (!stop.aborted) {
                        deps.log(`${source.name}: ${error instanceof Error ? error.message : String(error)}`);
                    }
                }
            }
            if (deadline.aborted) {
                throw lunaError(ERRORS.timeout, "Timed out looking for the position");
            }
            throw lunaError(ERRORS.positionUnavailable, "No source could find the position");
        },
    };
};

// A fix as HP's replies carried it.
export const positionReply = (fix: Fix) => ({
    errorCode: 0,
    timestamp: fix.timestamp,
    latitude: fix.latitude,
    longitude: fix.longitude,
    horizAccuracy: fix.horizAccuracy,
    vertAccuracy: fix.vertAccuracy,
    altitude: fix.altitude,
    heading: fix.heading,
    velocity: fix.velocity,
});
