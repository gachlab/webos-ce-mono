// Where a position comes from. Each source is one way of finding it, behind the
// same `locate`, so the service can try them in whatever order a request asks
// for and more can be added without touching it.
//
// HP's service had two: its GPS handler, and Google's network engine, which
// looked up the Wi-Fi access points and cell towers in range. Here:
//
//   gps   gpsd, for a receiver the host has.
//   wifi  the access points NetworkManager sees, looked up in BeaconDB, the
//         open successor of Mozilla's location service.
//   ip    BeaconDB's estimate from the public address: a city, give or take.
//
// wifi and ip send something about the user to a third party, as Google's
// engine did; the service only uses them once the terms were accepted.

export type SourceName = "gps" | "wifi" | "ip";

// A position, in the fields HP's replies carry. -1 is HP's "unknown".
export interface Fix {
    readonly latitude: number;
    readonly longitude: number;
    // Metres.
    readonly horizAccuracy: number;
    readonly vertAccuracy: number;
    readonly altitude: number;
    // Degrees from north, and metres per second.
    readonly heading: number;
    readonly velocity: number;
    // Milliseconds since the epoch.
    readonly timestamp: number;
}

export interface Source {
    readonly name: SourceName;
    // Sends the user's surroundings or address off the device.
    readonly remote: boolean;
    // A position, or undefined when this source has none to give. Only a
    // broken source throws.
    locate(signal: AbortSignal): Promise<Fix | undefined>;
}

export const UNKNOWN = -1;

const finite = (value: unknown): value is number => typeof value === "number" && Number.isFinite(value);

const orUnknown = (value: unknown): number => (finite(value) ? value : UNKNOWN);

const networkFix = (latitude: number, longitude: number, accuracy: number, now: number): Fix => ({
    latitude, longitude, horizAccuracy: accuracy, vertAccuracy: UNKNOWN, altitude: UNKNOWN,
    heading: UNKNOWN, velocity: UNKNOWN, timestamp: now,
});

// ---- gps: gpsd ----------------------------------------------------------------

// A gpsd TPV report with at least a 2D fix, as HP's fields. gpsd's `eph` is
// the horizontal error at 95%; older gpsd only sends `epx`/`epy`.
export const fixFromTpv = (report: Record<string, unknown>, now: number): Fix | undefined => {
    if (report.class !== "TPV" || !finite(report.mode) || report.mode < 2
        || !finite(report.lat) || !finite(report.lon)) {
        return undefined;
    }
    const horizontal = finite(report.eph) ? report.eph
        : finite(report.epx) && finite(report.epy) ? Math.max(report.epx, report.epy)
        : UNKNOWN;
    const has3d = report.mode >= 3;
    const time = typeof report.time === "string" ? Date.parse(report.time) : Number.NaN;
    return {
        latitude: report.lat,
        longitude: report.lon,
        horizAccuracy: horizontal,
        vertAccuracy: has3d ? orUnknown(report.epv) : UNKNOWN,
        altitude: has3d ? orUnknown(report.altHAE ?? report.alt) : UNKNOWN,
        heading: orUnknown(report.track),
        velocity: orUnknown(report.speed),
        timestamp: Number.isFinite(time) ? time : now,
    };
};

// gpsd's JSON lines, from the moment WATCH was sent; the stream ends when the
// signal is aborted or gpsd goes away.
export type GpsdLines = (signal: AbortSignal) => AsyncIterable<string>;

export const createGpsSource = (deps: { lines: GpsdLines; now: () => number }): Source => ({
    name: "gps",
    remote: false,
    locate: async (signal) => {
        const stop = new AbortController();
        const onAbort = () => stop.abort();
        signal.addEventListener("abort", onAbort, { once: true });
        try {
            for await (const line of deps.lines(stop.signal)) {
                let report: unknown;
                try {
                    report = JSON.parse(line);
                } catch {
                    continue;
                }
                if (report !== null && typeof report === "object") {
                    const fix = fixFromTpv(report as Record<string, unknown>, deps.now());
                    if (fix) {
                        return fix;
                    }
                }
            }
            return undefined;
        } finally {
            signal.removeEventListener("abort", onAbort);
            stop.abort();
        }
    },
});

// ---- wifi and ip: BeaconDB ----------------------------------------------------

export const BEACONDB_URL = "https://api.beacondb.net/v1/geolocate";

export interface AccessPoint {
    readonly macAddress: string;
    // dBm.
    readonly signalStrength: number;
    // MHz.
    readonly frequency: number;
}

type Fetch = (url: string, init: RequestInit) => Promise<Response>;

// The geolocate API (Mozilla's, which BeaconDB keeps): a 404 is "no idea".
const geolocate = async (fetch: Fetch, url: string, body: object, signal: AbortSignal, now: number): Promise<Fix | undefined> => {
    const response = await fetch(url, {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify(body),
        signal,
    });
    if (response.status === 404) {
        return undefined;
    }
    if (!response.ok) {
        throw new Error(`${url} answered ${response.status}`);
    }
    const answer = await response.json() as { location?: { lat?: unknown; lng?: unknown }; accuracy?: unknown };
    const lat = answer.location?.lat;
    const lng = answer.location?.lng;
    if (!finite(lat) || !finite(lng)) {
        return undefined;
    }
    return networkFix(lat, lng, orUnknown(answer.accuracy), now);
};

// A lookup needs at least two access points: the API refuses one, so a single
// hotspot cannot be used to find its owner.
export const MIN_ACCESS_POINTS = 2;

export const createWifiSource = (deps: {
    scan: (signal: AbortSignal) => Promise<readonly AccessPoint[]>;
    fetch: Fetch;
    now: () => number;
    url?: string;
}): Source => ({
    name: "wifi",
    remote: true,
    locate: async (signal) => {
        const points = await deps.scan(signal);
        if (points.length < MIN_ACCESS_POINTS) {
            return undefined;
        }
        return geolocate(deps.fetch, deps.url ?? BEACONDB_URL,
            { wifiAccessPoints: points, considerIp: false }, signal, deps.now());
    },
});

export const createIpSource = (deps: { fetch: Fetch; now: () => number; url?: string }): Source => ({
    name: "ip",
    remote: true,
    locate: (signal) => geolocate(deps.fetch, deps.url ?? BEACONDB_URL, { considerIp: true }, signal, deps.now()),
});

// `nmcli -t -f BSSID,SIGNAL,FREQ,SSID dev wifi list`: colons inside a field
// are escaped. Networks whose name ends in "_nomap" asked not to be mapped.
export const parseNmcliScan = (text: string): AccessPoint[] => {
    const points: AccessPoint[] = [];
    for (const line of text.split("\n")) {
        const fields = line.split(/(?<!\\):/).map((field) => field.replace(/\\:/g, ":").replace(/\\\\/g, "\\"));
        const [bssid, signal, frequency, ssid = ""] = fields;
        if (!bssid || !/^[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}$/.test(bssid) || ssid.endsWith("_nomap")) {
            continue;
        }
        const quality = Number(signal);
        const mhz = Number.parseInt(frequency ?? "", 10);
        if (!Number.isFinite(quality) || !Number.isFinite(mhz)) {
            continue;
        }
        // NetworkManager's 0-100 quality back to dBm, as it derived it.
        points.push({ macAddress: bssid.toLowerCase(), signalStrength: Math.round(quality / 2 - 100), frequency: mhz });
    }
    return points;
};
