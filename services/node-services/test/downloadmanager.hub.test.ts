// com.palm.downloadmanager and com.palm.appInstallService on a private hub,
// called the way the browser, LunaSysMgr and the system UI call them, with a
// local HTTP server and LunaSysMgr's com.palm.appinstaller played by this file.

import assert from "node:assert/strict";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, renameSync, rmSync, statSync, writeFileSync } from "node:fs";
import { open } from "node:fs/promises";
import { createServer, type Server } from "node:http";
import type { AddressInfo } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { after, before, beforeEach, describe, test } from "node:test";
import { setTimeout as sleep } from "node:timers/promises";

import { isLunaError, openBus, type Bus, type Payload } from "#kit/luna.ts";
import { createDownloadManager, DOWNLOADS, INSTALLS, type RunningDownloadManager } from "../services/com.palm.downloadmanager/service.ts";
import { startTestBus, type TestBus } from "./hub.ts";

const APPINSTALLER = "com.palm.appinstaller";
const APP = "com.webosce.test.app";
const OTHER = "com.webosce.test.other";
const D = `luna://${DOWNLOADS}`;
const I = `luna://${INSTALLS}`;

const env = {} as {
    hub: TestBus;
    service: RunningDownloadManager;
    app: Bus;
    other: Bus;
    installer: Bus;
    server: Server;
    base: string;
    root: string;
    downloads: string;
    freeKB: number;
    installerCalls: { method: string; payload: Payload }[];
    installerFails: Set<string>;
    // Held HTTP responses, and whether new ones are held.
    held: (() => void)[];
    holding: boolean;
};

const release = () => {
    for (const go of env.held.splice(0)) {
        go();
    }
};

const until = async (condition: () => boolean | Promise<boolean>, what: string, ms = 5000) => {
    for (const started = Date.now(); Date.now() - started < ms; await sleep(20)) {
        if (await condition()) {
            return;
        }
    }
    assert.fail(`timed out waiting for ${what}`);
};

const failsWith = (fields: Payload) => (error: unknown) => {
    assert.ok(isLunaError(error), String(error));
    for (const [key, value] of Object.entries(fields)) {
        assert.deepEqual((error as unknown as Payload)[key], value, key);
    }
    return true;
};

// Every reply of a subscription until one says `completed` (or `stop` says so).
const collect = async (bus: Bus, uri: string, payload: Payload, stop = (r: Payload) => "completed" in r) => {
    const replies: Payload[] = [];
    for await (const reply of bus.subscribe(uri, payload)) {
        replies.push(reply);
        if (stop(reply)) {
            break;
        }
    }
    return replies;
};

const body = (n: number) => Buffer.alloc(n, 7);

before(async () => {
    env.hub = await startTestBus({ services: [DOWNLOADS, INSTALLS, APPINSTALLER, APP, OTHER] });
    env.root = mkdtempSync(join(tmpdir(), "downloadmanager-"));
    env.downloads = join(env.root, "Downloads");
    env.freeKB = 500_000_000;
    env.installerCalls = [];
    env.installerFails = new Set();
    env.held = [];
    env.holding = false;

    env.server = createServer((request, response) => {
        const url = request.url ?? "";
        if (url.startsWith("/missing")) {
            response.writeHead(404).end();
            return;
        }
        const data = body(url.endsWith(".ipk") ? 4000 : 1000);
        response.writeHead(200, { "content-length": String(data.length), "content-type": url.endsWith(".png") ? "image/png" : "application/octet-stream" });
        response.write(data.subarray(0, 500));
        const finish = () => response.end(data.subarray(500));
        if (env.holding) {
            env.held.push(finish);
        } else {
            finish();
        }
    });
    await new Promise<void>((resolve) => env.server.listen(0, "127.0.0.1", resolve));
    env.base = `http://127.0.0.1:${(env.server.address() as AddressInfo).port}`;

    // LunaSysMgr's installer, as its documentation describes it: a ticket, then
    // status updates ending in SUCCESS or FAILED_*.
    env.installer = openBus(APPINSTALLER);
    for (const method of ["install", "remove"]) {
        env.installer.method(method, async function* ({ payload }) {
            env.installerCalls.push({ method, payload });
            yield { ticket: env.installerCalls.length, subscribed: true };
            yield { ticket: env.installerCalls.length, status: method === "install" ? "STARTING" : "IPKG_REMOVE" };
            const key = String(payload.id ?? payload.packageName);
            yield { ticket: env.installerCalls.length, status: env.installerFails.has(key) ? "FAILED_IPKG_INSTALL" : "SUCCESS" };
        });
    }

    const text = (path: string) => ({
        read: () => (existsSync(path) ? readFileSync(path, "utf8") : undefined),
        write: (value: string) => {
            mkdirSync(join(path, ".."), { recursive: true });
            writeFileSync(path, value);
        },
    });

    env.service = createDownloadManager({
        openBus,
        fetch: (url, init) => fetch(url, init),
        files: {
            exists: (path) => existsSync(path),
            size: (path) => (existsSync(path) ? statSync(path).size : undefined),
            mkdir: (dir) => mkdirSync(dir, { recursive: true }),
            open: async (path, append) => {
                const handle = await open(path, append ? "a" : "w");
                return { write: async (chunk) => { await handle.write(chunk); }, close: () => handle.close() };
            },
            rename: (from, to) => renameSync(from, to),
            remove: (path) => rmSync(path, { force: true }),
        },
        places: { downloads: env.downloads, home: env.root },
        historyFiles: text(join(env.root, "data", "history.json")),
        installStore: text(join(env.root, "installs", "installs.json")),
        installDir: join(env.root, "installs"),
        space: () => ({ totalKB: 1_000_000_000, freeKB: env.freeKB }),
        pollMs: 50,
        progressMs: 0,
        now: () => Date.now(),
        sleep: (ms, signal) => sleep(ms, undefined, { signal }),
        log: () => undefined,
    })();
    env.app = openBus(APP);
    env.other = openBus(OTHER);
});

after(() => {
    release();
    env.service?.close();
    for (const bus of [env.app, env.other, env.installer]) {
        bus?.close();
    }
    env.server?.closeAllConnections();
    env.server?.close();
    env.hub?.stop();
    if (env.root) {
        rmSync(env.root, { recursive: true, force: true });
    }
});

beforeEach(() => {
    release();
    env.holding = false;
    env.installerCalls.length = 0;
    env.installerFails.clear();
    env.freeKB = 500_000_000;
});

describe("download", () => {
    test("a subscriber gets the ticket, progress, and the final record", async () => {
        const replies = await collect(env.app, `${D}/download`, { target: `${env.base}/docs/manual.pdf`, mime: "application/pdf" });
        const first = replies[0]!;
        assert.equal(first.returnValue, true);
        assert.equal(first.subscribed, true);
        assert.equal(typeof first.ticket, "number");
        assert.equal(first.url, `${env.base}/docs/manual.pdf`);
        assert.equal(first.target, join(env.downloads, "manual.pdf"));
        assert.ok(replies.slice(1, -1).every((r) => r.ticket === first.ticket && "amountReceived" in r));
        const end = replies[replies.length - 1]!;
        assert.equal(end.completed, true);
        assert.equal(end.completionStatusCode, 200);
        assert.equal(end.destPath, `${env.downloads}/`);
        assert.equal(end.destFile, "manual.pdf");
        assert.equal(statSync(join(env.downloads, "manual.pdf")).size, 1000);
    });

    test("without subscribing, the download goes on and its status can be asked for", async () => {
        const reply = await env.app.call(`${D}/download`, { target: `${env.base}/quiet.bin` });
        assert.equal(reply.subscribed, false);
        await until(async () => {
            const status = await env.app.call(`${D}/downloadStatusQuery`, { ticket: reply.ticket });
            return status.completed === true;
        }, "the download to complete");
        assert.ok(existsSync(String(reply.target)));
    });

    test("a missing target is refused, and an unknown ticket is not found", async () => {
        await assert.rejects(env.app.call(`${D}/download`, {}), failsWith({ errorCode: -1 }));
        await assert.rejects(env.app.call(`${D}/cancelDownload`, { ticket: 99999 }), failsWith({ errorCode: "ticket_not_found" }));
        await assert.rejects(env.app.call(`${D}/downloadStatusQuery`, { ticket: "x" }), failsWith({ errorCode: -1 }));
    });

    test("a failed download says so, and the browser would offer a retry", async () => {
        const replies = await collect(env.app, `${D}/download`, { target: `${env.base}/missing.zip` });
        const end = replies[replies.length - 1]!;
        assert.deepEqual([end.completed, end.aborted, end.completionStatusCode], [false, true, 404]);
    });

    test("cancelDownload ends a subscriber's download", async () => {
        env.holding = true;
        const replies = collect(env.app, `${D}/download`, { target: `${env.base}/cancel-me.bin` });
        await until(() => env.held.length === 1, "the transfer to start");
        const { items } = await env.app.call<{ items: Payload[] }>(`${D}/listPending`);
        const ticket = items[items.length - 1]!.ticket;
        await env.app.call(`${D}/cancelDownload`, { ticket });
        const end = (await replies).pop()!;
        assert.equal(end.aborted, true);
        assert.equal(existsSync(join(env.downloads, "cancel-me.bin")), false);
    });

    test("pause and resume through the bus", async () => {
        env.holding = true;
        const replies = collect(env.app, `${D}/download`, { target: `${env.base}/paused.bin`, canHandlePause: true });
        await until(() => env.held.length === 1, "the transfer to start");
        const { items } = await env.app.call<{ items: Payload[] }>(`${D}/listPending`);
        const ticket = items[items.length - 1]!.ticket;
        await env.app.call(`${D}/pauseDownload`, { ticket });
        await until(async () => (await env.app.call(`${D}/downloadStatusQuery`, { ticket })).state === "paused", "the pause");
        env.holding = false;
        release();
        await env.app.call(`${D}/resumeDownload`, { ticket });
        const end = (await replies).pop()!;
        assert.equal(end.completed, true);
        assert.equal(statSync(join(env.downloads, "paused.bin")).size, 1000);
    });
});

describe("history", () => {
    test("each app sees and clears its own downloads", async () => {
        await env.app.call(`${D}/clearHistory`, { owner: APP });
        await env.other.call(`${D}/clearHistory`, { owner: OTHER });
        const mine = await collect(env.app, `${D}/download`, { target: `${env.base}/mine.txt` });
        await collect(env.other, `${D}/download`, { target: `${env.base}/theirs.txt` });

        const { items } = await env.app.call<{ items: Payload[] }>(`${D}/getAllHistory`, { owner: APP });
        assert.equal(items.length, 1);
        assert.equal(items[0]!.state, "completed");
        assert.equal(items[0]!.fileExistsOnFilesys, true);
        const record = JSON.parse(String(items[0]!.recordString)) as Payload;
        assert.equal(record.ticket, mine[0]!.ticket);
        assert.equal(record.destFile, "mine.txt");

        await env.app.call(`${D}/clearHistory`, { owner: APP });
        assert.deepEqual((await env.app.call<{ items: Payload[] }>(`${D}/getAllHistory`, { owner: APP })).items, []);
        assert.equal((await env.other.call<{ items: Payload[] }>(`${D}/getAllHistory`, { owner: OTHER })).items.length, 1);
    });

    test("deleteDownloadedFile removes the file, only for its owner", async () => {
        const replies = await collect(env.app, `${D}/download`, { target: `${env.base}/delete-me.txt` });
        const { ticket, target } = replies[replies.length - 1]!;
        await assert.rejects(env.other.call(`${D}/deleteDownloadedFile`, { ticket }), failsWith({ errorCode: "ticket_not_found" }));
        assert.ok(existsSync(String(target)));
        await env.app.call(`${D}/deleteDownloadedFile`, { ticket });
        assert.equal(existsSync(String(target)), false);
        await assert.rejects(env.app.call(`${D}/downloadStatusQuery`, { ticket }), failsWith({ errorCode: "ticket_not_found" }));
    });
});

describe("filesysStatusCheck", () => {
    test("answers the current level, then tells a subscriber when it changes", async () => {
        const first = await env.app.call(`${D}/filesysStatusCheck`, {});
        assert.equal(first.alert, "none");
        assert.equal(first.amountRemainingKB, 500_000_000);

        const replies: Payload[] = [];
        const abort = new AbortController();
        const watching = (async () => {
            for await (const reply of env.app.subscribe(`${D}/filesysStatusCheck`, {}, { signal: abort.signal })) {
                replies.push(reply);
            }
        })();
        await until(() => replies.length === 1, "the first reply");
        env.freeKB = 800_000;
        await until(() => replies.some((r) => r.alert === "medium"), "the medium alert");
        const medium = replies.find((r) => r.alert === "medium")!;
        assert.equal(medium.reason, "polled");
        assert.equal(medium.amountRemainingKB, 800_000);
        const count = replies.length;
        await sleep(200);
        assert.equal(replies.length, count, "an unchanged level is not repeated");
        abort.abort();
        await watching;
    });
});

describe("appInstallService", () => {
    const APP_ID = "com.webosce.test.installed";

    const watchStatus = () => {
        const replies: Payload[] = [];
        const abort = new AbortController();
        const done = (async () => {
            for await (const reply of env.app.subscribe(`${I}/status`, {}, { signal: abort.signal })) {
                replies.push(reply);
            }
        })();
        return { replies, stop: async () => { abort.abort(); await done; } };
    };
    const statesOf = (replies: Payload[], id: string) => replies
        .filter((r) => r.id === id)
        .map((r) => (r.details as Payload).state);

    test("an install downloads the icon and the package, then has the installer install it", async () => {
        const status = watchStatus();
        await until(() => status.replies.length === 1, "the first status");
        assert.ok(Array.isArray(((status.replies[0]!.status as Payload).apps)));
        await env.app.call(`${I}/install`, {
            id: APP_ID, title: "Test App", version: "1.0.0", vendor: "Test",
            ipkUrl: `${env.base}/pkg/app_1.0.0.ipk`, iconUrl: `${env.base}/pkg/icon.png`,
        });
        await until(() => statesOf(status.replies, APP_ID).includes("installed"), "installed");
        const states = statesOf(status.replies, APP_ID);
        assert.equal(states[0], "icon download current");
        for (const expected of ["icon download complete", "ipk download current", "ipk download complete", "installing", "installed"]) {
            assert.ok(states.includes(expected), `${expected} in ${states.join(", ")}`);
        }
        const installedDetails = status.replies.filter((r) => r.id === APP_ID).pop()!.details as Payload;
        assert.equal(installedDetails.title, "Test App");
        assert.equal(installedDetails.client, APP);
        assert.ok(String(installedDetails.icon).endsWith("icon.png"));
        assert.ok(existsSync(String(installedDetails.icon)));
        const call = env.installerCalls.find((c) => c.method === "install")!;
        assert.equal(call.payload.id, APP_ID);
        assert.ok(String(call.payload.target).endsWith("app_1.0.0.ipk"));
        assert.equal(statSync(String(call.payload.target)).size, 4000);
        await status.stop();

        // Done, so no longer listed.
        const again = watchStatus();
        await until(() => again.replies.length === 1, "the first status");
        assert.ok(!((again.replies[0]!.status as Payload).apps as Payload[]).some((a) => a.id === APP_ID));
        await again.stop();
    });

    test("a failed install stays listed until its data is removed", async () => {
        env.installerFails.add("com.webosce.test.broken");
        const status = watchStatus();
        await env.app.call(`${I}/install`, { id: "com.webosce.test.broken", ipkUrl: `${env.base}/pkg/broken.ipk` });
        await until(() => statesOf(status.replies, "com.webosce.test.broken").includes("install failed"), "install failed");
        await status.stop();

        const listed = watchStatus();
        await until(() => listed.replies.length === 1, "the first status");
        const apps = (listed.replies[0]!.status as Payload).apps as Payload[];
        assert.equal((apps.find((a) => a.id === "com.webosce.test.broken")!.details as Payload).state, "install failed");
        await listed.stop();

        await env.app.call(`${I}/removeAppInstallData`, { id: "com.webosce.test.broken" });
        const cleared = watchStatus();
        await until(() => cleared.replies.length === 1, "the first status");
        assert.ok(!((cleared.replies[0]!.status as Payload).apps as Payload[]).some((a) => a.id === "com.webosce.test.broken"));
        await cleared.stop();
    });

    test("a package that cannot be downloaded is a download failure, and never reaches the installer", async () => {
        const status = watchStatus();
        await env.app.call(`${I}/install`, { id: "com.webosce.test.gone", ipkUrl: `${env.base}/missing/gone.ipk` });
        await until(() => statesOf(status.replies, "com.webosce.test.gone").includes("download failed"), "download failed");
        assert.equal(env.installerCalls.length, 0);
        await status.stop();
        await env.app.call(`${I}/removeAppInstallData`, { id: "com.webosce.test.gone" });
    });

    test("an install can be paused, resumed and canceled while it downloads", async () => {
        env.holding = true;
        const status = watchStatus();
        await env.app.call(`${I}/install`, { id: "com.webosce.test.slow", ipkUrl: `${env.base}/pkg/slow.ipk` });
        await until(() => env.held.length === 1, "the package download");
        await env.app.call(`${I}/pause`, { id: "com.webosce.test.slow" });
        await until(() => statesOf(status.replies, "com.webosce.test.slow").includes("ipk download paused"), "paused");
        // The pause's last progress report must not undo it.
        await sleep(150);
        assert.equal(statesOf(status.replies, "com.webosce.test.slow").pop(), "ipk download paused");
        release();
        await env.app.call(`${I}/resume`, { id: "com.webosce.test.slow" });
        await until(() => env.held.length === 1, "the download again");
        await env.app.call(`${I}/cancel`, { id: "com.webosce.test.slow" });
        await until(() => statesOf(status.replies, "com.webosce.test.slow").includes("canceled"), "canceled");
        // The download itself stops: nothing is left pending, and no package lands.
        const { items } = await env.app.call<{ items: Payload[] }>(`${D}/listPending`);
        assert.ok(!items.some((item) => String(item.url).endsWith("/pkg/slow.ipk")));
        release();
        await sleep(100);
        assert.equal(existsSync(join(env.root, "installs", "ipks", "slow.ipk")), false);
        const states = statesOf(status.replies, "com.webosce.test.slow");
        assert.equal(states[states.length - 1], "canceled");
        assert.equal(env.installerCalls.length, 0);
        await assert.rejects(env.app.call(`${I}/cancel`, { id: "com.webosce.test.slow" }), failsWith({ errorCode: "NOT_FOUND" }));
        await status.stop();
    });

    test("an app already being installed is refused a second time", async () => {
        env.holding = true;
        await env.app.call(`${I}/install`, { id: "com.webosce.test.twice", ipkUrl: `${env.base}/pkg/twice.ipk` });
        await assert.rejects(env.app.call(`${I}/install`, { id: "com.webosce.test.twice", ipkUrl: `${env.base}/pkg/twice.ipk` }),
            failsWith({ errorCode: "ALREADY_IN_PROGRESS" }));
        await env.app.call(`${I}/cancel`, { id: "com.webosce.test.twice" });
        release();
    });

    test("remove goes through the installer, and reports its failure", async () => {
        const status = watchStatus();
        await env.app.call(`${I}/remove`, { id: "com.webosce.test.old" });
        await until(() => statesOf(status.replies, "com.webosce.test.old").includes("removed"), "removed");
        assert.deepEqual(statesOf(status.replies, "com.webosce.test.old"), ["removing", "removed"]);
        assert.deepEqual(env.installerCalls[0], { method: "remove", payload: { packageName: "com.webosce.test.old", subscribe: true } });

        env.installerFails.add("com.webosce.test.stuck");
        await env.app.call(`${I}/remove`, { id: "com.webosce.test.stuck" });
        await until(() => statesOf(status.replies, "com.webosce.test.stuck").includes("remove failed"), "remove failed");
        await status.stop();
        await env.app.call(`${I}/removeAppInstallData`, { id: "com.webosce.test.stuck" });
    });

    test("missing arguments are refused", async () => {
        await assert.rejects(env.app.call(`${I}/install`, { id: "x" }), failsWith({ errorCode: -1 }));
        await assert.rejects(env.app.call(`${I}/remove`, {}), failsWith({ errorCode: -1 }));
        await assert.rejects(env.app.call(`${I}/pause`, { id: "com.webosce.test.none" }), failsWith({ errorCode: "NOT_FOUND" }));
    });
});

