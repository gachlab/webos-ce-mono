// The commands of com.palm.downloadmanager and com.palm.appInstallService.
//
// Method names, payloads and replies are those of HP's LunaDownloadMgr, as its
// callers in this tree use them: the browser, LunaSysMgr, the universal
// search manager and the system UI. Methods nothing calls any more (upload,
// allow1x, swapToInterface) are left out.

import type { Handler, Payload, Request } from "#kit/luna.ts";
import { mojoError, mojoHandler } from "#kit/mojoservice.ts";
import { alertFor, POLL_MS, type Alert, type Marks, type Space } from "./filesys.ts";
import type { History } from "./history.ts";
import type { InstallRequest, Installs } from "./installs.ts";
import type { Transfers } from "./transfers.ts";

export interface Command {
    readonly name: string;
    readonly handler: Handler;
    // Registered on the public bus as well.
    readonly public?: boolean;
}

export interface CommandDeps {
    readonly transfers: Transfers;
    readonly history: History;
    readonly installs: Installs;
    readonly files: { exists(path: string): boolean; remove(path: string): void };
    readonly space: () => Space;
    readonly marks?: Marks;
    readonly pollMs?: number;
    readonly sleep: (ms: number, signal: AbortSignal) => Promise<void>;
}

const str = (value: unknown): string | undefined => (typeof value === "string" ? value : undefined);
const bool = (value: unknown): boolean | undefined => (typeof value === "boolean" ? value : undefined);

const required = (payload: Payload, field: string): string => {
    const value = str(payload[field]);
    if (!value) {
        throw mojoError(-1, `Missing required parameter '${field}'`);
    }
    return value;
};

const ticketOf = (payload: Payload): number => {
    const ticket = Number(payload.ticket);
    if (!Number.isInteger(ticket) || ticket <= 0) {
        throw mojoError(-1, "Missing or invalid 'ticket'");
    }
    return ticket;
};

// Who a download belongs to: the app that asked, or the service.
export const ownerOf = (request: Request): string =>
    request.applicationId ?? request.senderServiceName ?? request.sender ?? "";

const notFound = (ticket: number) => mojoError("ticket_not_found", `No download with ticket ${ticket}`);

export const downloadCommands = (deps: CommandDeps): Command[] => [
    {
        name: "download",
        public: true,
        handler: async function* (request) {
            const { payload } = request;
            const started = deps.transfers.start({
                target: required(payload, "target"),
                owner: ownerOf(request),
                mime: str(payload.mime),
                targetDir: str(payload.targetDir),
                targetFilename: str(payload.targetFilename),
                canHandlePause: bool(payload.canHandlePause),
                authToken: str(payload.authToken),
                deviceId: str(payload.deviceId),
                cookieHeader: str(payload.cookieHeader),
            });
            yield { ...started, subscribed: request.subscribe };
            yield* deps.transfers.watch(Number(started.ticket), request.signal);
        },
    },
    {
        name: "downloadStatusQuery",
        public: true,
        handler: mojoHandler(({ payload }) => {
            const ticket = ticketOf(payload);
            const status = deps.transfers.status(ticket);
            if (!status) {
                throw notFound(ticket);
            }
            return status;
        }),
    },
    {
        name: "listPending",
        public: true,
        handler: mojoHandler(() => {
            const items = deps.transfers.pending();
            return { count: items.length, items };
        }),
    },
    {
        name: "cancelDownload",
        public: true,
        handler: mojoHandler(({ payload }) => {
            const ticket = ticketOf(payload);
            if (!deps.transfers.cancel(ticket)) {
                throw notFound(ticket);
            }
            return { ticket };
        }),
    },
    {
        name: "cancelAllDownloads",
        handler: mojoHandler(() => ({ count: deps.transfers.cancelAll() })),
    },
    {
        name: "pauseDownload",
        public: true,
        handler: mojoHandler(({ payload }) => {
            const ticket = ticketOf(payload);
            if (!deps.transfers.pause(ticket)) {
                throw notFound(ticket);
            }
            return { ticket };
        }),
    },
    {
        name: "resumeDownload",
        public: true,
        handler: mojoHandler(({ payload }) => {
            const ticket = ticketOf(payload);
            if (!deps.transfers.resume(ticket)) {
                throw notFound(ticket);
            }
            return { ticket };
        }),
    },
    {
        name: "deleteDownloadedFile",
        public: true,
        handler: mojoHandler((request) => {
            const ticket = ticketOf(request.payload);
            const entry = deps.history.get(ticket);
            if (!entry || entry.owner !== ownerOf(request)) {
                throw notFound(ticket);
            }
            const target = str(entry.record.target);
            if (target && deps.files.exists(target)) {
                deps.files.remove(target);
            }
            deps.history.remove(ticket);
            return { ticket };
        }),
    },
    {
        name: "getAllHistory",
        public: true,
        handler: mojoHandler((request) => {
            const owner = str(request.payload.owner) ?? ownerOf(request);
            return {
                items: deps.history.of(owner).map((entry) => ({
                    ticket: entry.ticket,
                    owner: entry.owner,
                    state: entry.state,
                    fileExistsOnFilesys: deps.files.exists(str(entry.record.target) ?? ""),
                    recordString: JSON.stringify(entry.record),
                })),
            };
        }),
    },
    {
        name: "clearHistory",
        public: true,
        handler: mojoHandler((request) => {
            deps.history.clear(str(request.payload.owner) ?? ownerOf(request));
        }),
    },
    {
        name: "filesysStatusCheck",
        public: true,
        handler: async function* ({ subscribe, signal }) {
            const check = () => {
                const space = deps.space();
                return { alert: alertFor(space, deps.marks), amountRemainingKB: space.freeKB };
            };
            const first = check();
            yield { ...first, subscribed: subscribe };
            let last: Alert = first.alert;
            while (subscribe && !signal.aborted) {
                await deps.sleep(deps.pollMs ?? POLL_MS, signal).catch(() => undefined);
                if (signal.aborted) {
                    return;
                }
                const now = check();
                // Every poll while at the limit, as HP's did: the system UI
                // shows that one each time.
                if (now.alert !== last || now.alert === "limit") {
                    last = now.alert;
                    yield { ...now, reason: "polled" };
                }
            }
        },
    },
];

const installRequest = (payload: Payload): InstallRequest => ({
    id: required(payload, "id"),
    ipkUrl: required(payload, "ipkUrl"),
    iconUrl: str(payload.iconUrl),
    title: str(payload.title),
    version: str(payload.version),
    vendor: str(payload.vendor),
    vendorUrl: str(payload.vendorUrl),
    authToken: str(payload.authToken),
    deviceId: str(payload.deviceId),
});

export const installCommands = (deps: CommandDeps): Command[] => [
    {
        name: "status",
        handler: async function* ({ signal }) {
            yield* deps.installs.status(signal);
        },
    },
    {
        name: "install",
        handler: mojoHandler((request) => {
            deps.installs.install(installRequest(request.payload), ownerOf(request));
        }),
    },
    {
        name: "installLocal",
        handler: mojoHandler((request) => {
            deps.installs.installLocal(installRequest(request.payload), ownerOf(request));
        }),
    },
    {
        name: "remove",
        handler: mojoHandler((request) => {
            deps.installs.remove(required(request.payload, "id"), ownerOf(request));
        }),
    },
    {
        name: "pause",
        handler: mojoHandler(({ payload }) => deps.installs.pause(required(payload, "id"))),
    },
    {
        name: "resume",
        handler: mojoHandler(({ payload }) => deps.installs.resume(required(payload, "id"))),
    },
    {
        name: "cancel",
        handler: mojoHandler(({ payload }) => deps.installs.cancel(required(payload, "id"))),
    },
    {
        name: "removeAppInstallData",
        handler: mojoHandler(({ payload }) => deps.installs.removeData(required(payload, "id"))),
    },
    {
        name: "removeAppInstallHistory",
        handler: mojoHandler(() => deps.installs.removeHistory()),
    },
];
