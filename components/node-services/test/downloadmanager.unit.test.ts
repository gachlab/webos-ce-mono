// com.palm.downloadmanager's parts, without a bus: names and places, the
// history, disk alerts, and transfers against a local HTTP server.

import assert from "node:assert/strict";
import { existsSync, mkdtempSync, readdirSync, readFileSync, renameSync, rmSync, statSync, writeFileSync, mkdirSync } from "node:fs";
import { open } from "node:fs/promises";
import { createServer, type IncomingMessage, type Server, type ServerResponse } from "node:http";
import type { AddressInfo } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { after, before, beforeEach, describe, test } from "node:test";

import type { Payload } from "#kit/luna.ts";
import { alertFor } from "../services/com.palm.downloadmanager/filesys.ts";
import { createHistory } from "../services/com.palm.downloadmanager/history.ts";
import { dispositionName, freeName, resolveDir, safeName, urlName } from "../services/com.palm.downloadmanager/paths.ts";
import { createTransfers, type TransferFiles, type Transfers } from "../services/com.palm.downloadmanager/transfers.ts";

const places = { downloads: "/home/u/Descargas", home: "/home/u" };

describe("where downloads go", () => {
    test("the device's downloads folder is the user's XDG Downloads", () => {
        assert.equal(resolveDir(places, undefined), "/home/u/Descargas");
        assert.equal(resolveDir(places, ""), "/home/u/Descargas");
        assert.equal(resolveDir(places, "/media/internal/downloads"), "/home/u/Descargas");
        assert.equal(resolveDir(places, "/media/internal/downloads/podcasts/"), "/home/u/Descargas/podcasts/");
    });

    test("the rest of /media/internal is the home, and other paths stay as they are", () => {
        assert.equal(resolveDir(places, "/media/internal/ringtones"), "/home/u/ringtones");
        assert.equal(resolveDir(places, "/media/internalx"), "/media/internalx");
        assert.equal(resolveDir(places, "/var/palm/data/universalsearchmgr/searchplugins"),
            "/var/palm/data/universalsearchmgr/searchplugins");
        // Normalized first: this one is no longer under /media/internal.
        assert.equal(resolveDir(places, "/media/internal/downloads/../../etc"), "/media/etc");
    });

    test("names come from the URL, and stay inside their directory", () => {
        assert.equal(urlName("http://h/a/b/report%20final.pdf?x=1"), "report final.pdf");
        assert.equal(urlName("http://h/"), undefined);
        assert.equal(urlName("not a url"), undefined);
        assert.equal(safeName("../../etc/passwd"), ".._.._etc_passwd");
        assert.equal(safeName(".."), "download");
        assert.equal(safeName(""), "download");
    });

    test("a server's Content-Disposition names the file", () => {
        assert.equal(dispositionName('attachment; filename="notes.txt"'), "notes.txt");
        assert.equal(dispositionName("attachment; filename=plain.bin; size=3"), "plain.bin");
        assert.equal(dispositionName("attachment; filename*=UTF-8''caf%C3%A9.txt; filename=\"cafe.txt\""), "café.txt");
        assert.equal(dispositionName("inline"), undefined);
        assert.equal(dispositionName(null), undefined);
    });

    test("a taken name gets _2, _3... before its extension, as HP's did", () => {
        const taken = new Set(["a.pdf", "a_2.pdf", "noext"]);
        assert.equal(freeName("a.pdf", (n) => taken.has(n)), "a_3.pdf");
        assert.equal(freeName("noext", (n) => taken.has(n)), "noext_2");
        assert.equal(freeName("b.pdf", (n) => taken.has(n)), "b.pdf");
        assert.equal(freeName(".hidden", (n) => n === ".hidden"), ".hidden_2");
    });
});

describe("history", () => {
    const memory = () => {
        const box: { text?: string } = {};
        return { box, files: { read: () => box.text, write: (text: string) => { box.text = text; } } };
    };
    const record = (ticket: number): Payload => ({ ticket, target: `/d/${ticket}` });

    test("tickets go up, and keep going up after a restart", () => {
        const { files } = memory();
        const first = createHistory(files);
        assert.equal(first.nextTicket(), 1);
        assert.equal(first.nextTicket(), 2);
        assert.equal(createHistory(files).nextTicket(), 3);
    });

    test("a restart never reuses a remembered ticket", () => {
        const { box, files } = memory();
        box.text = JSON.stringify({ nextTicket: 2, entries: [{ ticket: 9, owner: "a", state: "completed", record: record(9) }] });
        assert.equal(createHistory(files).nextTicket(), 10);
    });

    test("entries are per owner, oldest ticket first, and cleared per owner", () => {
        const { files } = memory();
        const history = createHistory(files);
        history.add({ ticket: 5, owner: "a", state: "completed", record: record(5) });
        history.add({ ticket: 2, owner: "a", state: "failed", record: record(2) });
        history.add({ ticket: 3, owner: "b", state: "completed", record: record(3) });
        assert.deepEqual(history.of("a").map((e) => e.ticket), [2, 5]);
        const reloaded = createHistory(files);
        assert.equal(reloaded.get(3)?.owner, "b");
        reloaded.clear("a");
        assert.deepEqual(reloaded.of("a"), []);
        assert.equal(reloaded.of("b").length, 1);
        reloaded.remove(3);
        assert.equal(reloaded.get(3), undefined);
    });

    test("only the newest entries are kept", () => {
        const history = createHistory(memory().files, 2);
        for (const ticket of [1, 2, 3]) {
            history.add({ ticket, owner: "a", state: "completed", record: record(ticket) });
        }
        assert.deepEqual(history.of("a").map((e) => e.ticket), [2, 3]);
    });

    test("a damaged file starts empty", () => {
        const { box, files } = memory();
        box.text = "{not json";
        const history = createHistory(files);
        assert.equal(history.nextTicket(), 1);
        assert.deepEqual(history.of("a"), []);
    });
});

describe("disk alerts", () => {
    // A 16 GB tablet, as HP's percentages were meant for.
    const space = (usedPercent: number, totalKB = 16_000_000) =>
        ({ totalKB, freeKB: Math.round((totalKB * (100 - usedPercent)) / 100) });

    test("levels follow how full the filesystem is", () => {
        assert.equal(alertFor(space(50)), "none");
        assert.equal(alertFor(space(90)), "low");
        assert.equal(alertFor(space(95)), "medium");
        assert.equal(alertFor(space(98)), "severe");
    });

    test("a large disk with gigabytes left is not full, whatever its percentage", () => {
        // Found live: 221 GB at 98% still had 6 GB, and the system UI said
        // "Device Is 95% Full".
        assert.equal(alertFor(space(98, 221_000_000)), "none");
        assert.equal(alertFor({ totalKB: 221_000_000, freeKB: 1_500_000 }), "low");
        assert.equal(alertFor({ totalKB: 221_000_000, freeKB: 800_000 }), "medium");
        assert.equal(alertFor({ totalKB: 221_000_000, freeKB: 400_000 }), "severe");
    });

    test("too little room left is the limit, however large the disk", () => {
        assert.equal(alertFor({ totalKB: 1_000_000_000, freeKB: 10_000 }), "limit");
        assert.equal(alertFor({ totalKB: 30_000, freeKB: 20_000 }), "limit");
    });
});

// ---- transfers ----------------------------------------------------------------

interface Route {
    status?: number;
    headers?: Record<string, string>;
    // Bytes, sent in chunks; the server waits for `release` before the last.
    body?: Buffer;
    hold?: boolean;
    // Honours Range.
    ranges?: boolean;
}

const env = {} as {
    server: Server;
    base: string;
    routes: Map<string, Route>;
    requests: IncomingMessage[];
    held: (() => void)[];
    dir: string;
    logs: string[];
};

const release = () => {
    for (const go of env.held.splice(0)) {
        go();
    }
};

const serve = async (request: IncomingMessage, response: ServerResponse) => {
    env.requests.push(request);
    const route = env.routes.get(request.url ?? "");
    if (!route) {
        response.writeHead(404).end("missing");
        return;
    }
    const body = route.body ?? Buffer.alloc(0);
    const range = route.ranges ? /bytes=(\d+)-/.exec(request.headers.range ?? "") : null;
    const from = range ? Number(range[1]) : 0;
    const sent = body.subarray(from);
    response.writeHead(range ? 206 : (route.status ?? 200), {
        "content-length": String(sent.length),
        ...route.headers,
    });
    const half = Math.floor(sent.length / 2);
    response.write(sent.subarray(0, half));
    if (route.hold) {
        await new Promise<void>((resolve) => env.held.push(resolve));
    }
    response.end(sent.subarray(half));
};

const realFiles: TransferFiles = {
    exists: (path) => existsSync(path),
    size: (path) => (existsSync(path) ? statSync(path).size : undefined),
    mkdir: (dir) => mkdirSync(dir, { recursive: true }),
    open: async (path, append) => {
        const handle = await open(path, append ? "a" : "w");
        return { write: async (chunk) => { await handle.write(chunk); }, close: () => handle.close() };
    },
    rename: (from, to) => renameSync(from, to),
    remove: (path) => rmSync(path, { force: true }),
};

const makeTransfers = (overrides: { maxConcurrent?: number; fetch?: typeof fetch } = {}): Transfers => {
    const box: { text?: string } = {};
    return createTransfers({
        fetch: overrides.fetch ?? ((url, init) => fetch(url, init)),
        files: realFiles,
        places: { downloads: env.dir, home: env.dir },
        history: createHistory({ read: () => box.text, write: (text) => { box.text = text; } }),
        now: () => Date.now(),
        progressMs: 0,
        maxConcurrent: overrides.maxConcurrent ?? 2,
        log: (message) => env.logs.push(message),
    });
};

const follow = async (transfers: Transfers, ticket: unknown, signal = new AbortController().signal) => {
    const events: Payload[] = [];
    for await (const event of transfers.watch(Number(ticket), signal)) {
        events.push(event);
    }
    return events;
};

const last = (events: Payload[]) => events[events.length - 1]!;
const bytes = (n: number) => Buffer.from(Array.from({ length: n }, (_, i) => i % 251));

describe("transfers", () => {
    before(async () => {
        env.routes = new Map();
        env.held = [];
        env.server = createServer((request, response) => void serve(request, response));
        await new Promise<void>((resolve) => env.server.listen(0, "127.0.0.1", resolve));
        env.base = `http://127.0.0.1:${(env.server.address() as AddressInfo).port}`;
    });

    after(() => {
        release();
        env.server.closeAllConnections();
        env.server.close();
    });

    beforeEach(() => {
        release();
        env.routes.clear();
        env.requests = [];
        env.logs = [];
        env.dir = mkdtempSync(join(tmpdir(), "downloads-"));
    });

    test("a download lands in its folder, with HP's replies on the way", async () => {
        env.routes.set("/files/report.pdf", { body: bytes(5000), headers: { "content-type": "application/pdf; q=1" } });
        const transfers = makeTransfers();
        const first = transfers.start({ target: `${env.base}/files/report.pdf`, owner: "com.example.app" });
        assert.deepEqual(first, { ticket: 1, url: `${env.base}/files/report.pdf`, target: join(env.dir, "report.pdf") });
        const events = await follow(transfers, first.ticket);
        const progress = events.slice(0, -1);
        assert.ok(progress.length >= 1);
        assert.deepEqual(Object.keys(progress[0]!).sort(), ["amountReceived", "amountTotal", "ticket"]);
        assert.deepEqual(last(events), {
            ticket: 1,
            url: `${env.base}/files/report.pdf`,
            sourceUrl: `${env.base}/files/report.pdf`,
            deviceId: "",
            authToken: "",
            destTempPrefix: ".",
            destFile: "report.pdf",
            destPath: `${env.dir}/`,
            mimetype: "application/pdf",
            amountReceived: 5000,
            amountTotal: 5000,
            canHandlePause: false,
            cookieHeader: "",
            completionStatusCode: 200,
            httpStatus: 200,
            interrupted: false,
            completed: true,
            aborted: false,
            target: join(env.dir, "report.pdf"),
        });
        assert.deepEqual(readFileSync(join(env.dir, "report.pdf")), bytes(5000));
        assert.deepEqual(readdirSync(env.dir), ["report.pdf"]);
        assert.equal(transfers.active(), 0);
    });

    test("a second copy is name_2, and a watcher that comes late gets the end", async () => {
        env.routes.set("/a.txt", { body: bytes(10) });
        writeFileSync(join(env.dir, "a.txt"), "old");
        const transfers = makeTransfers();
        const first = transfers.start({ target: `${env.base}/a.txt`, owner: "x" });
        assert.equal(first.target, join(env.dir, "a_2.txt"));
        await follow(transfers, first.ticket);
        const late = await follow(transfers, first.ticket);
        assert.equal(late.length, 1);
        assert.equal(last(late).completed, true);
        assert.equal(readFileSync(join(env.dir, "a.txt"), "utf8"), "old");
    });

    test("two downloads of one name at once do not share a file", async () => {
        env.routes.set("/same.bin", { body: bytes(10), hold: true });
        const transfers = makeTransfers();
        const one = transfers.start({ target: `${env.base}/same.bin`, owner: "x" });
        const two = transfers.start({ target: `${env.base}/same.bin`, owner: "x" });
        assert.notEqual(one.target, two.target);
        const done = Promise.all([follow(transfers, one.ticket), follow(transfers, two.ticket)]);
        await until(() => env.held.length === 2);
        release();
        const [a, b] = await done;
        assert.equal(last(a).completed, true);
        assert.equal(last(b).completed, true);
    });

    test("targetDir and targetFilename are followed, and the server's name used when none was given", async () => {
        env.routes.set("/get?id=3", { body: bytes(4), headers: { "content-disposition": 'attachment; filename="plugin.xml"' } });
        const transfers = makeTransfers();
        const named = transfers.start({ target: `${env.base}/get?id=3`, owner: "x", targetDir: join(env.dir, "sub"), targetFilename: "mine.xml" });
        assert.equal(named.target, join(env.dir, "sub", "mine.xml"));
        assert.equal(last(await follow(transfers, named.ticket)).destFile, "mine.xml");
        const suggested = transfers.start({ target: `${env.base}/get?id=3`, owner: "x" });
        const end = last(await follow(transfers, suggested.ticket));
        assert.equal(end.destFile, "plugin.xml");
        assert.ok(existsSync(join(env.dir, "plugin.xml")));
    });

    test("the caller's cookie, auth token and device id are sent", async () => {
        env.routes.set("/secret", { body: bytes(1) });
        const transfers = makeTransfers();
        const started = transfers.start({ target: `${env.base}/secret`, owner: "x", cookieHeader: "s=1", authToken: "tok", deviceId: "dev" });
        await follow(transfers, started.ticket);
        assert.equal(env.requests[0]!.headers.cookie, "s=1");
        assert.equal(env.requests[0]!.headers["auth-token"], "tok");
        assert.equal(env.requests[0]!.headers["device-id"], "dev");
    });

    test("an HTTP error is not completed, aborted, and leaves no file", async () => {
        const transfers = makeTransfers();
        const started = transfers.start({ target: `${env.base}/nothing.zip`, owner: "x" });
        const end = last(await follow(transfers, started.ticket));
        assert.equal(end.completed, false);
        assert.equal(end.aborted, true);
        assert.equal(end.interrupted, false);
        assert.equal(end.completionStatusCode, 404);
        assert.deepEqual(readdirSync(env.dir), []);
    });

    test("a network failure is interrupted", async () => {
        const transfers = makeTransfers({ fetch: () => Promise.reject(new TypeError("fetch failed")) });
        const started = transfers.start({ target: "http://nowhere.invalid/x", owner: "x" });
        const end = last(await follow(transfers, started.ticket));
        assert.deepEqual([end.completed, end.aborted, end.interrupted], [false, true, true]);
        assert.ok(env.logs.some((line) => line.includes("fetch failed")));
    });

    test("cancelling stops the download and removes what it wrote", async () => {
        env.routes.set("/big", { body: bytes(20000), hold: true });
        const transfers = makeTransfers();
        const started = transfers.start({ target: `${env.base}/big`, owner: "x" });
        const events = follow(transfers, started.ticket);
        await until(() => env.held.length === 1);
        await until(() => existsSync(join(env.dir, ".big")));
        assert.equal(transfers.cancel(Number(started.ticket)), true);
        const end = last(await events);
        assert.deepEqual([end.completed, end.aborted, end.interrupted], [false, true, false]);
        assert.deepEqual(readdirSync(env.dir), []);
        assert.equal(transfers.cancel(Number(started.ticket)), false);
        assert.equal(transfers.status(Number(started.ticket))?.aborted, true);
    });

    test("a paused download resumes where it stopped", async () => {
        const body = bytes(30000);
        env.routes.set("/resumable", { body, hold: true, ranges: true });
        const transfers = makeTransfers();
        const started = transfers.start({ target: `${env.base}/resumable`, owner: "x", canHandlePause: true });
        const ticket = Number(started.ticket);
        const events = follow(transfers, ticket);
        await until(() => env.held.length === 1);
        await until(() => (transfers.status(ticket)?.amountReceived as number) === 15000);
        assert.equal(transfers.pause(ticket), true);
        await until(() => transfers.status(ticket)?.state === "paused");
        assert.equal(statSync(join(env.dir, ".resumable")).size, 15000);
        env.routes.set("/resumable", { body, ranges: true });
        release();
        assert.equal(transfers.resume(ticket), true);
        const end = last(await events);
        assert.equal(end.completed, true);
        assert.equal(env.requests[1]!.headers.range, "bytes=15000-");
        assert.deepEqual(readFileSync(join(env.dir, "resumable")), body);
    });

    test("a server that ignores Range starts the file over", async () => {
        const body = bytes(30000);
        env.routes.set("/plain", { body, hold: true });
        const transfers = makeTransfers();
        const started = transfers.start({ target: `${env.base}/plain`, owner: "x" });
        const ticket = Number(started.ticket);
        const events = follow(transfers, ticket);
        await until(() => env.held.length === 1);
        await until(() => (transfers.status(ticket)?.amountReceived as number) === 15000);
        transfers.pause(ticket);
        await until(() => transfers.status(ticket)?.state === "paused");
        env.routes.set("/plain", { body });
        transfers.resume(ticket);
        const end = last(await events);
        assert.equal(end.amountReceived, 30000);
        assert.deepEqual(readFileSync(join(env.dir, "plain")), body);
    });

    test("no more than maxConcurrent run at once; the rest wait their turn", async () => {
        env.routes.set("/slow", { body: bytes(10), hold: true });
        const transfers = makeTransfers({ maxConcurrent: 1 });
        const one = transfers.start({ target: `${env.base}/slow`, owner: "x" });
        const two = transfers.start({ target: `${env.base}/slow`, owner: "x" });
        await until(() => env.held.length === 1);
        assert.equal(env.requests.length, 1);
        assert.equal(transfers.status(Number(two.ticket))?.state, "queued");
        assert.deepEqual(transfers.pending().map((p) => p.ticket), [one.ticket, two.ticket]);
        release();
        await until(() => env.held.length === 1);
        release();
        await follow(transfers, two.ticket);
        assert.equal(env.requests.length, 2);
    });

    test("activity counts the downloads not yet finished", async () => {
        env.routes.set("/count", { body: bytes(10), hold: true });
        const transfers = makeTransfers();
        const deltas: number[] = [];
        transfers.onActivity((delta) => deltas.push(delta));
        const started = transfers.start({ target: `${env.base}/count`, owner: "x" });
        await until(() => env.held.length === 1);
        assert.equal(transfers.active(), 1);
        release();
        await follow(transfers, started.ticket);
        assert.deepEqual(deltas, [1, -1]);
        assert.equal(transfers.active(), 0);
    });

    test("cancelAll ends every download, queued ones too", async () => {
        env.routes.set("/wait", { body: bytes(10), hold: true });
        const transfers = makeTransfers({ maxConcurrent: 1 });
        const one = transfers.start({ target: `${env.base}/wait`, owner: "x" });
        const two = transfers.start({ target: `${env.base}/wait`, owner: "x" });
        await until(() => env.held.length === 1);
        const ends = Promise.all([follow(transfers, one.ticket), follow(transfers, two.ticket)]);
        assert.equal(transfers.cancelAll(), 2);
        for (const events of await ends) {
            assert.equal(last(events).aborted, true);
        }
        assert.equal(transfers.active(), 0);
    });

    test("a watcher that goes away stops being told", async () => {
        env.routes.set("/leave", { body: bytes(10), hold: true });
        const transfers = makeTransfers();
        const started = transfers.start({ target: `${env.base}/leave`, owner: "x" });
        const abort = new AbortController();
        const events = follow(transfers, started.ticket, abort.signal);
        await until(() => env.held.length === 1);
        abort.abort();
        assert.ok((await events).every((event) => !("completed" in event)));
        release();
        assert.equal(last(await follow(transfers, started.ticket)).completed, true);
    });
});

async function until(condition: () => boolean, ms = 4000): Promise<void> {
    for (const started = Date.now(); Date.now() - started < ms; await new Promise((r) => setTimeout(r, 10))) {
        if (condition()) {
            return;
        }
    }
    assert.fail("timed out");
}
