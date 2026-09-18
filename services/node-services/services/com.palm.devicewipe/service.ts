// com.palm.devicewipe: wiring.

import { serveOnDemand, type OnDemandDeps, type OnDemandService } from "#kit/mojoservice.ts";
import { devicewipeCommands } from "./commands.ts";

export const SERVICE_NAME = "com.palm.devicewipe";

export const createDevicewipeService = (deps: OnDemandDeps & { readonly log: (message: string) => void }) =>
    (): OnDemandService => serveOnDemand(deps)(SERVICE_NAME, devicewipeCommands(deps.log));
