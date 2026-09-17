// The downloads themselves: a queue of transfers, each writing to a temporary
// file beside its destination and renamed when complete.
//
// A transfer outlives the request that started it. A caller that did not
// subscribe gets its ticket and goes away while the download goes on, as with
// HP's LunaDownloadMgr; subscribers only watch.
//
// What a subscriber sees follows HP's replies (the luna-send transcript in
// luna-sysmgr's ApplicationManagerService.cpp):
//
//   {ticket, url, target, subscribed}                         the request
//   {ticket, amountReceived, amountTotal}                     progress
//   {ticket, url, sourceUrl, target, destPath, destFile, ...,
//    completed, aborted, interrupted, completionStatusCode}   the end
//
// "target" is the file, not the URL: LunaSysMgr opens or installs it.
// On failure: completed false and aborted true, which is what makes the
// browser offer a retry and LunaSysMgr leave the file alone; interrupted is
// true as well when the network failed.

import type { Payload } from "#kit/luna.ts";
import type { History, HistoryState } from "./history.ts";
import { dispositionName, freeName, resolveDir, safeName, TEMP_PREFIX, tempName, urlName, withSlash, type Places } from "./paths.ts";

export interface Sink {
    write(chunk: Uint8Array): Promise<void>;
    close(): Promise<void>;
}

export interface TransferFiles {
    exists(path: string): boolean;
    size(path: string): number | undefined;
    mkdir(dir: string): void;
    open(path: string, append: boolean): Promise<Sink>;
    rename(from: string, to: string): void;
    remove(path: string): void;
}

export interface TransferDeps {
    readonly fetch: (url: string, init: RequestInit) => Promise<Response>;
    readonly files: TransferFiles;
    readonly places: Places;
    readonly history: History;
    readonly now: () => number;
    // HP's luna.conf: [DownloadManager] MaxConcurrent=2.
    readonly maxConcurrent?: number;
    // How often progress is reported, at most.
    readonly progressMs?: number;
    readonly log: (message: string) => void;
}

export interface StartRequest {
    readonly target: string;
    readonly owner: string;
    readonly mime?: string | undefined;
    readonly targetDir?: string | undefined;
    readonly targetFilename?: string | undefined;
    readonly canHandlePause?: boolean | undefined;
    readonly authToken?: string | undefined;
    readonly deviceId?: string | undefined;
    readonly cookieHeader?: string | undefined;
}

type State = "queued" | "running" | "paused" | "completed" | "cancelled" | "interrupted" | "failed";

interface Transfer {
    readonly ticket: number;
    readonly request: StartRequest;
    readonly dir: string;
    // Whether the name may still follow the server's Content-Disposition.
    readonly named: boolean;
    file: string;
    state: State;
    received: number;
    total: number;
    mimetype: string;
    httpStatus: number;
    abort: AbortController | undefined;
    readonly listeners: Set<(event: Payload) => void>;
    lastProgress: number;
}

export interface Transfers {
    // Queues a download; the reply is the request's first answer.
    start(request: StartRequest): Payload;
    // Everything a subscriber is sent after the first answer, up to and
    // including the final record. Ends early when `signal` aborts.
    watch(ticket: number, signal: AbortSignal): AsyncIterable<Payload>;
    cancel(ticket: number): boolean;
    cancelAll(): number;
    pause(ticket: number): boolean;
    resume(ticket: number): boolean;
    // The download as it stands, or as it ended.
    status(ticket: number): Payload | undefined;
    pending(): Payload[];
    // How many transfers are not finished: the service stays up for them.
    active(): number;
    // Told whenever active() changes.
    onActivity(listener: (delta: number) => void): void;
}

export const DEFAULT_MAX_CONCURRENT = 2;
export const DEFAULT_PROGRESS_MS = 250;

const finished = (state: State) =>
    state === "completed" || state === "cancelled" || state === "interrupted" || state === "failed";

const failure = (error: unknown): string => (error instanceof Error ? error.message : String(error));

export const createTransfers = (deps: TransferDeps): Transfers => {
    const maxConcurrent = deps.maxConcurrent ?? DEFAULT_MAX_CONCURRENT;
    const progressMs = deps.progressMs ?? DEFAULT_PROGRESS_MS;
    const transfers = new Map<number, Transfer>();
    const queue: Transfer[] = [];
    const activity = new Set<(delta: number) => void>();
    const state = { running: 0 };

    const path = (transfer: Transfer) => `${transfer.dir}${transfer.file}`;
    const temp = (transfer: Transfer) => `${transfer.dir}${tempName(transfer.file)}`;

    const taken = (dir: string) => (name: string) =>
        deps.files.exists(`${dir}${name}`) || deps.files.exists(`${dir}${tempName(name)}`)
        || [...transfers.values()].some((t) => !finished(t.state) && t.dir === dir && t.file === name);

    const emit = (transfer: Transfer, event: Payload) => {
        for (const listener of [...transfer.listeners]) {
            listener(event);
        }
    };

    const progress = (transfer: Transfer): Payload =>
        ({ ticket: transfer.ticket, amountReceived: transfer.received, amountTotal: transfer.total });

    const record = (transfer: Transfer): Payload => {
        const { request } = transfer;
        const ok = transfer.state === "completed";
        return {
            ticket: transfer.ticket,
            url: request.target,
            sourceUrl: request.target,
            deviceId: request.deviceId ?? "",
            authToken: request.authToken ?? "",
            destTempPrefix: TEMP_PREFIX,
            destFile: transfer.file,
            destPath: transfer.dir,
            mimetype: transfer.mimetype,
            amountReceived: transfer.received,
            amountTotal: transfer.total,
            canHandlePause: request.canHandlePause ?? false,
            cookieHeader: request.cookieHeader ?? "",
            completionStatusCode: transfer.httpStatus,
            httpStatus: transfer.httpStatus,
            interrupted: transfer.state === "interrupted",
            completed: ok,
            aborted: !ok,
            target: path(transfer),
        };
    };

    const historyState = (s: State): HistoryState =>
        s === "completed" || s === "cancelled" || s === "interrupted" ? s : "failed";

    const end = (transfer: Transfer, final: State, reason?: string) => {
        transfer.state = final;
        transfer.abort = undefined;
        if (final !== "completed") {
            deps.files.remove(temp(transfer));
        }
        if (reason) {
            deps.log(`download ${transfer.ticket} ${final}: ${reason}`);
        }
        const finalRecord = record(transfer);
        deps.history.add({
            ticket: transfer.ticket, owner: transfer.request.owner, state: historyState(final), record: finalRecord,
        });
        emit(transfer, finalRecord);
        transfer.listeners.clear();
        // The history answers for it from now on.
        transfers.delete(transfer.ticket);
        for (const listener of activity) {
            listener(-1);
        }
    };

    const headersFor = (transfer: Transfer, from: number): Record<string, string> => {
        const { request } = transfer;
        const headers: Record<string, string> = {};
        if (request.cookieHeader) {
            headers.Cookie = request.cookieHeader;
        }
        // The names HP's LunaDownloadMgr sent them under.
        if (request.authToken) {
            headers["Auth-Token"] = request.authToken;
        }
        if (request.deviceId) {
            headers["Device-Id"] = request.deviceId;
        }
        if (from > 0) {
            headers.Range = `bytes=${from}-`;
        }
        return headers;
    };

    const run = async (transfer: Transfer) => {
        const abort = new AbortController();
        transfer.abort = abort;
        transfer.state = "running";
        const from = transfer.received > 0 && deps.files.size(temp(transfer)) === transfer.received ? transfer.received : 0;
        try {
            const response = await deps.fetch(transfer.request.target, {
                headers: headersFor(transfer, from), signal: abort.signal, redirect: "follow",
            });
            transfer.httpStatus = response.status;
            if (!response.ok) {
                await response.body?.cancel().catch(() => undefined);
                end(transfer, "failed", `HTTP ${response.status}`);
                return;
            }
            const resumed = from > 0 && response.status === 206;
            transfer.received = resumed ? from : 0;
            const length = Number(response.headers.get("content-length"));
            transfer.total = Number.isFinite(length) && length > 0 ? transfer.received + length : 0;
            const type = response.headers.get("content-type")?.split(";")[0]?.trim();
            transfer.mimetype = type || transfer.request.mime || "application/octet-stream";

            if (!transfer.named && !resumed) {
                const suggested = dispositionName(response.headers.get("content-disposition"));
                if (suggested && safeName(suggested) !== transfer.file) {
                    const others = taken(transfer.dir);
                    transfer.file = freeName(safeName(suggested), (name) => name !== transfer.file && others(name));
                }
            }

            const sink = await deps.files.open(temp(transfer), resumed);
            try {
                for await (const chunk of (response.body ?? []) as AsyncIterable<Uint8Array>) {
                    await sink.write(chunk);
                    transfer.received += chunk.byteLength;
                    const now = deps.now();
                    if (now - transfer.lastProgress >= progressMs) {
                        transfer.lastProgress = now;
                        emit(transfer, progress(transfer));
                    }
                }
            } finally {
                await sink.close();
            }
            if (transfer.total === 0) {
                transfer.total = transfer.received;
            }
            deps.files.rename(temp(transfer), path(transfer));
            end(transfer, "completed");
        } catch (error) {
            if (abort.signal.aborted && abort.signal.reason === "pause") {
                transfer.state = "paused";
                transfer.abort = undefined;
                transfer.received = deps.files.size(temp(transfer)) ?? 0;
                emit(transfer, progress(transfer));
            } else if (abort.signal.aborted) {
                end(transfer, "cancelled");
            } else {
                transfer.httpStatus = transfer.httpStatus || -1;
                end(transfer, "interrupted", failure(error));
            }
        }
    };

    const next = () => {
        while (state.running < maxConcurrent && queue.length > 0) {
            const transfer = queue.shift()!;
            if (transfer.state !== "queued") {
                continue;
            }
            state.running++;
            void run(transfer).finally(() => {
                state.running--;
                next();
            });
        }
    };

    const enqueue = (transfer: Transfer) => {
        transfer.state = "queued";
        queue.push(transfer);
        next();
    };

    const live = (ticket: number) => {
        const transfer = transfers.get(ticket);
        return transfer && !finished(transfer.state) ? transfer : undefined;
    };

    return {
        start: (request) => {
            const dir = withSlash(resolveDir(deps.places, request.targetDir));
            deps.files.mkdir(dir);
            const wanted = safeName(request.targetFilename || urlName(request.target) || "download");
            const transfer: Transfer = {
                ticket: deps.history.nextTicket(),
                request,
                dir,
                named: Boolean(request.targetFilename),
                file: freeName(wanted, taken(dir)),
                state: "queued",
                received: 0,
                total: 0,
                mimetype: request.mime ?? "",
                httpStatus: 0,
                abort: undefined,
                listeners: new Set(),
                lastProgress: 0,
            };
            transfers.set(transfer.ticket, transfer);
            for (const listener of activity) {
                listener(+1);
            }
            enqueue(transfer);
            return { ticket: transfer.ticket, url: request.target, target: path(transfer) };
        },

        watch: (ticket, signal) => ({
            [Symbol.asyncIterator]: async function* () {
                const transfer = transfers.get(ticket);
                if (!transfer) {
                    const ended = deps.history.get(ticket);
                    if (ended) {
                        yield ended.record;
                    }
                    return;
                }
                const events: Payload[] = [];
                const wake: { resolve?: (() => void) | undefined } = {};
                const listener = (event: Payload) => {
                    events.push(event);
                    wake.resolve?.();
                };
                const stop = () => wake.resolve?.();
                transfer.listeners.add(listener);
                signal.addEventListener("abort", stop, { once: true });
                try {
                    while (!signal.aborted) {
                        const event = events.shift();
                        if (event === undefined) {
                            await new Promise<void>((resolve) => { wake.resolve = resolve; });
                            wake.resolve = undefined;
                            continue;
                        }
                        yield event;
                        if ("completed" in event) {
                            return;
                        }
                    }
                } finally {
                    transfer.listeners.delete(listener);
                    signal.removeEventListener("abort", stop);
                }
            },
        }),

        cancel: (ticket) => {
            const transfer = live(ticket);
            if (!transfer) {
                return false;
            }
            if (transfer.abort) {
                transfer.abort.abort("cancel");
            } else {
                end(transfer, "cancelled");
            }
            return true;
        },

        cancelAll: () => {
            const all = [...transfers.values()];
            for (const transfer of all) {
                if (transfer.abort) {
                    transfer.abort.abort("cancel");
                } else {
                    end(transfer, "cancelled");
                }
            }
            return all.length;
        },

        pause: (ticket) => {
            const transfer = live(ticket);
            if (!transfer) {
                return false;
            }
            if (transfer.state === "queued") {
                transfer.state = "paused";
                return true;
            }
            if (transfer.state !== "running" || !transfer.abort) {
                return false;
            }
            transfer.abort.abort("pause");
            return true;
        },

        resume: (ticket) => {
            const transfer = live(ticket);
            if (!transfer || transfer.state !== "paused") {
                return false;
            }
            enqueue(transfer);
            return true;
        },

        status: (ticket) => {
            const transfer = transfers.get(ticket);
            return transfer ? { ...record(transfer), state: transfer.state } : deps.history.get(ticket)?.record;
        },

        pending: () => [...transfers.values()]
            .map((t) => ({ ...progress(t), url: t.request.target, target: path(t), state: t.state, owner: t.request.owner })),

        active: () => transfers.size,

        onActivity: (listener) => {
            activity.add(listener);
        },
    };
};
