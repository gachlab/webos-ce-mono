// Entry point: `node main.ts`, started with the session's static services.

import { execFile } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, renameSync, writeFileSync } from "node:fs";
import { connect } from "node:net";
import { dirname, join } from "node:path";
import { createInterface } from "node:readline";
import { setTimeout as sleep } from "node:timers/promises";
import { getCACertificates, setDefaultCACertificates } from "node:tls";

import { openBus } from "#kit/luna.ts";
import { createReverse } from "./geocode.ts";
import { createLocationService } from "./service.ts";
import { createGpsSource, createIpSource, createWifiSource, parseNmcliScan, type AccessPoint } from "./sources.ts";

const log = (message: string) => console.log(`com.palm.location: ${message}`);

// HTTPS trusts what the host trusts, not only the certificates node carries.
try {
    setDefaultCACertificates([...getCACertificates("default"), ...getCACertificates("system")]);
} catch (error) {
    log(`using node's own certificates: ${String(error)}`);
}

const DATA = process.env.WEBOS_LOCATION_DATA ?? "/var/palm/data/com.palm.location";
const GPSD_HOST = process.env.GPSD_HOST ?? "127.0.0.1";
const GPSD_PORT = Number(process.env.GPSD_PORT ?? 2947);
const PREFS = join(DATA, "prefs.json");

const now = () => Date.now();

// gpsd's reports, one JSON object a line, once WATCH is on.
const gpsdLines = async function* (signal: AbortSignal, command = '?WATCH={"enable":true,"json":true};'): AsyncGenerator<string> {
    const socket = connect({ host: GPSD_HOST, port: GPSD_PORT });
    const close = () => socket.destroy();
    signal.addEventListener("abort", close, { once: true });
    try {
        await new Promise<void>((resolve, reject) => {
            socket.once("connect", resolve);
            socket.once("error", reject);
        });
        socket.write(command);
        yield* createInterface({ input: socket, crlfDelay: Infinity });
    } catch (error) {
        // No gpsd is no receiver, not a broken one.
        const code = (error as NodeJS.ErrnoException).code;
        if (!signal.aborted && code !== "ECONNREFUSED" && code !== "ENOENT") {
            throw error;
        }
    } finally {
        signal.removeEventListener("abort", close);
        close();
    }
};

// A receiver is attached when gpsd lists a device.
const gpsAvailable = async (): Promise<boolean> => {
    const signal = AbortSignal.timeout(1000);
    try {
        for await (const line of gpsdLines(signal, "?DEVICES;")) {
            const report = JSON.parse(line) as { class?: string; devices?: unknown[] };
            if (report.class === "DEVICES") {
                return Array.isArray(report.devices) && report.devices.length > 0;
            }
        }
    } catch {
        // No gpsd.
    }
    return false;
};

// The access points in range, from the last scan NetworkManager made.
const scan = (signal: AbortSignal): Promise<AccessPoint[]> =>
    new Promise((resolve) => {
        execFile("nmcli", ["-t", "-f", "BSSID,SIGNAL,FREQ,SSID", "dev", "wifi", "list", "--rescan", "no"],
            { signal, timeout: 5000 }, (error, stdout) => resolve(error ? [] : parseNmcliScan(stdout)));
    });

const fetchUrl = (url: string, init: RequestInit) => fetch(url, init);

createLocationService({
    openBus,
    sources: [
        createGpsSource({ lines: (signal) => gpsdLines(signal), now }),
        createWifiSource({ scan, fetch: fetchUrl, now }),
        createIpSource({ fetch: fetchUrl, now }),
    ],
    prefsFile: {
        read: () => (existsSync(PREFS) ? readFileSync(PREFS, "utf8") : undefined),
        write: (text) => {
            mkdirSync(dirname(PREFS), { recursive: true });
            writeFileSync(`${PREFS}.new`, text);
            renameSync(`${PREFS}.new`, PREFS);
        },
    },
    reverse: createReverse({
        fetch: fetchUrl,
        userAgent: "webOS-CE com.palm.location (https://github.com/gachlab/webos-ce-mono)",
        language: () => (process.env.LANG ?? "en_US").split(".")[0]!.replace("_", "-"),
    }),
    gpsAvailable,
    now,
    sleep: (ms, signal) => sleep(ms, undefined, { signal }),
    log,
})();
