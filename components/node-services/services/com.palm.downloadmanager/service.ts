// com.palm.downloadmanager and com.palm.appInstallService: wiring.
//
// One process answers both names, as HP's LunaDownloadMgr did. It runs for the
// whole session: LunaSysMgr and the system UI subscribe only once the bus
// reports the service up (registerServerStatus), so it is started with the
// other static services instead of on demand, and never exits when idle.

import type { Bus, BusOptions, Payload } from "#kit/luna.ts";
import { DEFAULT_COMMAND_TIMEOUT } from "#kit/mojoservice.ts";
import { downloadCommands, installCommands, type Command, type CommandDeps } from "./commands.ts";
import type { Marks, Space } from "./filesys.ts";
import { createHistory, type HistoryFiles } from "./history.ts";
import { createInstalls, type InstallStore } from "./installs.ts";
import type { Places } from "./paths.ts";
import { createTransfers, type TransferFiles } from "./transfers.ts";

export const DOWNLOADS = "com.palm.downloadmanager";
export const INSTALLS = "com.palm.appInstallService";

export interface DownloadManagerDeps {
    readonly openBus: (name: string, options: BusOptions) => Bus;
    readonly fetch: (url: string, init: RequestInit) => Promise<Response>;
    readonly files: TransferFiles & { remove(path: string): void };
    readonly places: Places;
    readonly historyFiles: HistoryFiles;
    readonly installStore: InstallStore;
    // Where appInstallService keeps what it downloads.
    readonly installDir: string;
    readonly space: () => Space;
    readonly marks?: Marks;
    readonly pollMs?: number;
    readonly progressMs?: number;
    readonly maxConcurrent?: number;
    readonly now: () => number;
    readonly sleep: (ms: number, signal: AbortSignal) => Promise<void>;
    readonly log: (message: string) => void;
}

export interface RunningDownloadManager {
    readonly close: () => void;
}

const register = (buses: readonly Bus[], commands: readonly Command[], publicBus?: Bus) => {
    for (const command of commands) {
        const options = { timeout: DEFAULT_COMMAND_TIMEOUT };
        for (const bus of buses) {
            bus.method(command.name, command.handler, options);
        }
        if (command.public && publicBus) {
            publicBus.method(command.name, command.handler, options);
        }
    }
};

export const createDownloadManager = (deps: DownloadManagerDeps) => (): RunningDownloadManager => {
    const downloads = { private: deps.openBus(DOWNLOADS, {}), public: deps.openBus(DOWNLOADS, { public: true }) };
    const installsBus = deps.openBus(INSTALLS, {});

    const history = createHistory(deps.historyFiles);
    const transfers = createTransfers({
        fetch: deps.fetch, files: deps.files, places: deps.places, history, now: deps.now, log: deps.log,
        ...(deps.progressMs === undefined ? {} : { progressMs: deps.progressMs }),
        ...(deps.maxConcurrent === undefined ? {} : { maxConcurrent: deps.maxConcurrent }),
    });
    const installs = createInstalls({
        subscribe: (uri: string, payload: Payload, signal: AbortSignal) => installsBus.subscribe(uri, payload, { signal }),
        transfers, store: deps.installStore, dir: deps.installDir, log: deps.log,
    });

    const commandDeps: CommandDeps = {
        transfers, history, installs, files: deps.files, space: deps.space, sleep: deps.sleep,
        ...(deps.marks === undefined ? {} : { marks: deps.marks }),
        ...(deps.pollMs === undefined ? {} : { pollMs: deps.pollMs }),
    };
    register([downloads.private], downloadCommands(commandDeps), downloads.public);
    register([installsBus], installCommands(commandDeps));

    return {
        close: () => {
            downloads.private.close();
            downloads.public.close();
            installsBus.close();
        },
    };
};
