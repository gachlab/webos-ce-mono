// com.palm.location: wiring.
//
// It runs for the whole session, like HP's: the answers to the location alert
// hold for the session, and would be forgotten by a service that exits when
// idle.

import type { Bus, BusOptions, Payload } from "#kit/luna.ts";
import { DEFAULT_COMMAND_TIMEOUT } from "#kit/mojoservice.ts";
import { locationCommands, type CommandDeps } from "./commands.ts";
import { createConsent } from "./consent.ts";
import { createLocator } from "./locator.ts";
import { createPrefs, type PrefsFile } from "./prefs.ts";
import type { Source } from "./sources.ts";

export const SERVICE_NAME = "com.palm.location";

export interface LocationDeps {
    readonly openBus: (name: string, options: BusOptions) => Bus;
    readonly sources: readonly Source[];
    readonly prefsFile: PrefsFile;
    readonly reverse: CommandDeps["reverse"];
    readonly gpsAvailable: () => Promise<boolean>;
    readonly now: () => number;
    readonly sleep: (ms: number, signal: AbortSignal) => Promise<void>;
    readonly trackMs?: number;
    readonly log: (message: string) => void;
}

export interface RunningLocation {
    readonly close: () => void;
}

export const createLocationService = (deps: LocationDeps) => (): RunningLocation => {
    const buses = { private: deps.openBus(SERVICE_NAME, {}), public: deps.openBus(SERVICE_NAME, { public: true }) };
    const prefs = createPrefs(deps.prefsFile);
    const consent = createConsent({
        prefs,
        publish: async (message: Payload) => {
            await buses.private.call("luna://com.palm.systemmanager/publishToSystemUI", message);
        },
        log: deps.log,
    });
    const locator = createLocator({ sources: deps.sources, prefs: prefs.get, now: deps.now, log: deps.log });
    const commands = locationCommands({
        locator, consent, prefs,
        reverse: deps.reverse,
        gpsAvailable: deps.gpsAvailable,
        // HP asked the application manager whether there is a camera app.
        cameraAvailable: () => buses.private
            .call("luna://com.palm.applicationManager/getAppInfo", { appId: "com.palm.app.camera" })
            .then(() => true, () => false),
        sleep: deps.sleep,
        ...(deps.trackMs === undefined ? {} : { trackMs: deps.trackMs }),
    });
    for (const command of commands) {
        const options = { timeout: command.timeout ?? DEFAULT_COMMAND_TIMEOUT };
        buses.private.method(command.name, command.handler, options);
        if (command.public) {
            buses.public.method(command.name, command.handler, options);
        }
    }
    return {
        close: () => {
            buses.private.close();
            buses.public.close();
        },
    };
};
