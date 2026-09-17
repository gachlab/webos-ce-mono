// com.palm.appInstallService: apps being downloaded, installed or removed.
//
// HP's LunaDownloadMgr answered this name too. Its callers here are
// LunaSysMgr, which shows pending apps in the launcher from the `status`
// subscription (ApplicationStatus.cpp), and the system UI, which shows the
// "Installed:" banner and the failure dashboard (AppManagerService.js). The
// App Catalog and Software Manager install through it.
//
// `status` answers {status: {apps: [{id, details}]}} first and then one
// {id, details} per change. details.state is one of HP's strings:
//
//   icon download current, icon download complete, ipk download current,
//   ipk download complete, ipk download paused, installing, installed,
//   canceled, install failed, download failed, removing, removed,
//   remove failed
//
// The package itself is installed and removed by LunaSysMgr's
// com.palm.appinstaller, whose statuses are STARTING..., SUCCESS or FAILED_*.
// An app that ended well (installed, removed) or was canceled is forgotten
// once its subscribers have been told; a failure stays listed until
// removeAppInstallData, which is how the system UI shows it again after a
// restart.

import type { Payload } from "#kit/luna.ts";
import { mojoError } from "#kit/mojoservice.ts";
import type { Transfers } from "./transfers.ts";

export const APPINSTALLER = "luna://com.palm.appinstaller";
export const SELF_OWNER = "com.palm.appInstallService";

export type InstallState =
    | "icon download current" | "icon download complete"
    | "ipk download current" | "ipk download complete" | "ipk download paused"
    | "installing" | "installed" | "canceled" | "install failed" | "download failed"
    | "removing" | "removed" | "remove failed";

export interface Details extends Payload {
    state: InstallState;
    progress: number;
    version: string;
    title: string;
    vendor: string;
    vendorUrl: string;
    icon: string;
    client: string;
}

interface App {
    readonly id: string;
    details: Details;
    // The download under way for it, if any.
    ticket?: number | undefined;
}

export interface InstallStore {
    read(): string | undefined;
    write(text: string): void;
}

export interface InstallDeps {
    readonly subscribe: (uri: string, payload: Payload, signal: AbortSignal) => AsyncIterable<Payload>;
    readonly transfers: Transfers;
    readonly store: InstallStore;
    // Where packages and icons are downloaded to.
    readonly dir: string;
    readonly log: (message: string) => void;
}

export interface InstallRequest {
    readonly id: string;
    readonly ipkUrl: string;
    readonly iconUrl?: string | undefined;
    readonly title?: string | undefined;
    readonly version?: string | undefined;
    readonly vendor?: string | undefined;
    readonly vendorUrl?: string | undefined;
    readonly authToken?: string | undefined;
    readonly deviceId?: string | undefined;
}

export interface Installs {
    status(signal: AbortSignal): AsyncIterable<Payload>;
    install(request: InstallRequest, client: string): void;
    // A package already on disk: ipkUrl is its path.
    installLocal(request: InstallRequest, client: string): void;
    remove(id: string, client: string): void;
    pause(id: string): void;
    resume(id: string): void;
    cancel(id: string): void;
    removeData(id: string): void;
    removeHistory(): void;
    // How many apps are still being worked on.
    busy(): number;
}

const TRANSIENT: readonly InstallState[] = [
    "icon download current", "icon download complete", "ipk download current", "ipk download complete",
    "ipk download paused", "installing", "removing",
];
const FORGOTTEN: readonly InstallState[] = ["installed", "removed", "canceled"];

const str = (value: unknown): string => (typeof value === "string" ? value : "");

export const createInstalls = (deps: InstallDeps): Installs => {
    const apps = new Map<string, App>();
    const listeners = new Set<(update: Payload) => void>();

    const save = () => deps.store.write(JSON.stringify([...apps.values()].map(({ id, details }) => ({ id, details }))));

    // What was going on when the service stopped cannot go on: it failed.
    try {
        const stored = JSON.parse(deps.store.read() ?? "[]") as { id: string; details: Details }[];
        for (const { id, details } of stored) {
            const state: InstallState = !TRANSIENT.includes(details.state) ? details.state
                : details.state === "removing" ? "remove failed"
                : details.state === "installing" ? "install failed" : "download failed";
            apps.set(id, { id, details: { ...details, state } });
        }
    } catch {
        // A damaged file starts empty.
    }

    const set = (id: string, changes: Partial<Details>) => {
        const app = apps.get(id);
        if (!app) {
            return;
        }
        app.details = { ...app.details, ...changes };
        const update = { id, details: { ...app.details } };
        if (FORGOTTEN.includes(app.details.state)) {
            apps.delete(id);
        }
        save();
        for (const listener of [...listeners]) {
            listener(update);
        }
    };

    const begin = (request: InstallRequest, client: string, state: InstallState) => {
        const current = apps.get(request.id);
        if (current && TRANSIENT.includes(current.details.state)) {
            throw mojoError("ALREADY_IN_PROGRESS", `${request.id} is already being worked on`);
        }
        apps.set(request.id, {
            id: request.id,
            details: {
                state, progress: 0,
                version: request.version ?? "", title: request.title ?? request.id,
                vendor: request.vendor ?? "", vendorUrl: request.vendorUrl ?? "",
                icon: "", client,
            },
        });
        set(request.id, {});
    };

    // Runs an appinstaller operation to its end: true on SUCCESS.
    const appinstaller = async (method: "install" | "remove", payload: Payload): Promise<boolean> => {
        const abort = new AbortController();
        try {
            for await (const update of deps.subscribe(`${APPINSTALLER}/${method}`, payload, abort.signal)) {
                const status = str(update.status);
                if (status === "SUCCESS") {
                    return true;
                }
                if (status.startsWith("FAILED")) {
                    deps.log(`appinstaller/${method} ${JSON.stringify(payload)}: ${status}`);
                    return false;
                }
            }
            return false;
        } catch (error) {
            deps.log(`appinstaller/${method} failed: ${String(error)}`);
            return false;
        } finally {
            abort.abort();
        }
    };

    const installFile = async (id: string, target: string) => {
        set(id, { state: "installing", progress: 100 });
        const ok = await appinstaller("install", { target, id, subscribe: true });
        set(id, { state: ok ? "installed" : "install failed" });
    };

    // A download for this app, followed to its end; the record, or undefined
    // when it did not complete.
    const fetchFor = async (id: string, url: string, targetDir: string, request: InstallRequest,
                            onProgress?: (percent: number) => void): Promise<Payload | undefined> => {
        const started = deps.transfers.start({
            target: url, owner: SELF_OWNER, targetDir, authToken: request.authToken, deviceId: request.deviceId,
            canHandlePause: true,
        });
        const ticket = Number(started.ticket);
        const app = apps.get(id);
        if (app) {
            app.ticket = ticket;
        }
        const abort = new AbortController();
        try {
            for await (const event of deps.transfers.watch(ticket, abort.signal)) {
                if ("completed" in event) {
                    return event.completed === true ? event : undefined;
                }
                const total = Number(event.amountTotal);
                if (onProgress && total > 0) {
                    onProgress(Math.floor((Number(event.amountReceived) * 100) / total));
                }
            }
            return undefined;
        } finally {
            abort.abort();
            if (app) {
                app.ticket = undefined;
            }
        }
    };

    const run = async (request: InstallRequest) => {
        const { id } = request;
        if (request.iconUrl) {
            const icon = await fetchFor(id, request.iconUrl, `${deps.dir}/icons`, request);
            if (!apps.has(id)) {
                return;
            }
            set(id, { state: "icon download complete", icon: str(icon?.target) });
        }
        set(id, { state: "ipk download current", progress: 0 });
        const ipk = await fetchFor(id, request.ipkUrl, `${deps.dir}/ipks`, request,
            // Only the number: a pause reports progress too, and must stay paused.
            (progress) => set(id, { progress }));
        const app = apps.get(id);
        if (!app || app.details.state === "canceled") {
            return;
        }
        if (!ipk) {
            set(id, { state: "download failed" });
            return;
        }
        set(id, { state: "ipk download complete", progress: 100 });
        await installFile(id, str(ipk.target));
    };

    const failed = (id: string, state: InstallState) => (error: unknown) => {
        deps.log(`${id}: ${String(error)}`);
        set(id, { state });
    };

    const known = (id: string): App => {
        const app = apps.get(id);
        if (!app) {
            throw mojoError("NOT_FOUND", `${id} is not being installed`);
        }
        return app;
    };

    return {
        status: (signal) => ({
            [Symbol.asyncIterator]: async function* () {
                const updates: Payload[] = [];
                const wake: { resolve?: (() => void) | undefined } = {};
                const listener = (update: Payload) => {
                    updates.push(update);
                    wake.resolve?.();
                };
                const stop = () => wake.resolve?.();
                listeners.add(listener);
                signal.addEventListener("abort", stop, { once: true });
                try {
                    yield { status: { apps: [...apps.values()].map(({ id, details }) => ({ id, details: { ...details } })) } };
                    while (!signal.aborted) {
                        const update = updates.shift();
                        if (update === undefined) {
                            await new Promise<void>((resolve) => { wake.resolve = resolve; });
                            wake.resolve = undefined;
                            continue;
                        }
                        yield update;
                    }
                } finally {
                    listeners.delete(listener);
                    signal.removeEventListener("abort", stop);
                }
            },
        }),

        install: (request, client) => {
            begin(request, client, request.iconUrl ? "icon download current" : "ipk download current");
            void run(request).catch(failed(request.id, "download failed"));
        },

        installLocal: (request, client) => {
            begin(request, client, "ipk download complete");
            set(request.id, { progress: 100 });
            void installFile(request.id, request.ipkUrl).catch(failed(request.id, "install failed"));
        },

        remove: (id, client) => {
            begin({ id, ipkUrl: "" }, client, "removing");
            void appinstaller("remove", { packageName: id, subscribe: true })
                .then((ok) => set(id, { state: ok ? "removed" : "remove failed" }))
                .catch(failed(id, "remove failed"));
        },

        pause: (id) => {
            const app = known(id);
            if (app.ticket === undefined || !deps.transfers.pause(app.ticket)) {
                throw mojoError("NOT_DOWNLOADING", `${id} is not downloading`);
            }
            set(id, { state: "ipk download paused" });
        },

        resume: (id) => {
            const app = known(id);
            if (app.ticket === undefined || !deps.transfers.resume(app.ticket)) {
                throw mojoError("NOT_PAUSED", `${id} is not paused`);
            }
            set(id, { state: "ipk download current" });
        },

        cancel: (id) => {
            const app = known(id);
            if (app.details.state === "installing" || app.details.state === "removing") {
                throw mojoError("CANNOT_CANCEL", `${id} is ${app.details.state}`);
            }
            const ticket = app.ticket;
            set(id, { state: "canceled" });
            if (ticket !== undefined) {
                deps.transfers.cancel(ticket);
            }
        },

        removeData: (id) => {
            const app = apps.get(id);
            if (app && !TRANSIENT.includes(app.details.state)) {
                apps.delete(id);
                save();
            }
        },

        removeHistory: () => {
            for (const app of [...apps.values()]) {
                if (!TRANSIENT.includes(app.details.state)) {
                    apps.delete(app.id);
                }
            }
            save();
        },

        busy: () => [...apps.values()].filter((app) => TRANSIENT.includes(app.details.state)).length,
    };
};
