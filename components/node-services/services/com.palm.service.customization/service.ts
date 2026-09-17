// com.palm.service.customization: wiring.

import { serveOnDemand, type OnDemandDeps, type OnDemandService } from "#kit/mojoservice.ts";
import { customizationCommands } from "./commands.ts";

export const SERVICE_NAME = "com.palm.service.customization";

export const createCustomizationService = (deps: OnDemandDeps & { readonly log: (message: string) => void }) =>
    (): OnDemandService => serveOnDemand(deps)(SERVICE_NAME, customizationCommands(deps.log));
