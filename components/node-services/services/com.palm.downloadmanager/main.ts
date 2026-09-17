// Entry point: `node main.ts`, started with the session's static services.

import { existsSync, mkdirSync, readFileSync, renameSync, rmSync, statfsSync, statSync, writeFileSync } from "node:fs";
import { open } from "node:fs/promises";
import { homedir } from "node:os";
import { dirname, join } from "node:path";
import { setTimeout as sleep } from "node:timers/promises";
import { getCACertificates, setDefaultCACertificates } from "node:tls";
import { execFileSync } from "node:child_process";

import { openBus } from "#kit/luna.ts";
import { createDownloadManager } from "./service.ts";

const log = (message: string) => console.log(`com.palm.downloadmanager: ${message}`);

// HTTPS trusts what the host trusts, not only the certificates node carries.
try {
    setDefaultCACertificates([...getCACertificates("default"), ...getCACertificates("system")]);
} catch (error) {
    log(`using node's own certificates: ${String(error)}`);
}

const DATA = process.env.WEBOS_DOWNLOADMANAGER_DATA ?? "/var/palm/data/com.palm.downloadmanager";
const INSTALL_DATA = process.env.WEBOS_APPINSTALLSERVICE_DATA ?? "/var/palm/data/com.palm.appInstallService";

// The user's XDG Downloads folder, as xdg-user-dir reports it.
const downloadsDir = (): string => {
    const configured = process.env.XDG_DOWNLOAD_DIR;
    if (configured) {
        return configured;
    }
    try {
        const dir = execFileSync("xdg-user-dir", ["DOWNLOAD"], { encoding: "utf8" }).trim();
        if (dir && dir !== homedir()) {
            return dir;
        }
    } catch {
        // Falls back below.
    }
    return join(homedir(), "Downloads");
};

const textFile = (path: string) => ({
    read: () => (existsSync(path) ? readFileSync(path, "utf8") : undefined),
    write: (text: string) => {
        mkdirSync(dirname(path), { recursive: true });
        writeFileSync(`${path}.new`, text);
        renameSync(`${path}.new`, path);
    },
});

const places = { downloads: downloadsDir(), home: homedir() };

createDownloadManager({
    openBus,
    fetch: (url, init) => fetch(url, init),
    files: {
        exists: (path) => existsSync(path),
        size: (path) => {
            try {
                return statSync(path).size;
            } catch {
                return undefined;
            }
        },
        mkdir: (dir) => mkdirSync(dir, { recursive: true }),
        open: async (path, append) => {
            const handle = await open(path, append ? "a" : "w");
            return {
                write: async (chunk) => {
                    await handle.write(chunk);
                },
                close: () => handle.close(),
            };
        },
        rename: (from, to) => renameSync(from, to),
        remove: (path) => rmSync(path, { force: true }),
    },
    places,
    historyFiles: textFile(join(DATA, "history.json")),
    installStore: textFile(join(INSTALL_DATA, "installs.json")),
    installDir: INSTALL_DATA,
    space: () => {
        mkdirSync(places.downloads, { recursive: true });
        const stats = statfsSync(places.downloads);
        const kb = (blocks: number) => Math.floor((blocks * stats.bsize) / 1024);
        return { totalKB: kb(stats.blocks), freeKB: kb(stats.bavail) };
    },
    now: () => Date.now(),
    sleep: (ms, signal) => sleep(ms, undefined, { signal }),
    log,
})();
