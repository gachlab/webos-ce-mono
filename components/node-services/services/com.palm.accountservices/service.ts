// com.palm.accountservices: wiring.

import { serveOnDemand, type OnDemandDeps, type OnDemandService } from "#kit/mojoservice.ts";
import { accountServicesCommands } from "./commands.ts";

export const SERVICE_NAME = "com.palm.accountservices";

export interface AccountServicesServiceDeps extends OnDemandDeps {
    readonly log: (message: string) => void;
}

export const createAccountServices = (deps: AccountServicesServiceDeps) => (): OnDemandService => {
    const holder: { service?: OnDemandService } = {};
    const service = serveOnDemand(deps)(SERVICE_NAME, accountServicesCommands({
        call: (uri, payload) => holder.service!.bus.call(uri, payload),
        log: deps.log,
    }));
    holder.service = service;
    return service;
};
