// com.palm.deviceprofile: wiring.

import type { Command } from "#kit/mojoservice.ts";
import { mojoHandler, serveOnDemand, type OnDemandDeps, type OnDemandService } from "#kit/mojoservice.ts";
import { deviceIdOf, deviceInfo, type Facts } from "./profile.ts";

export const SERVICE_NAME = "com.palm.deviceprofile";

export interface DeviceProfileDeps extends OnDemandDeps {
    // Read on every call: cheap, and the build can change under a running
    // session.
    readonly facts: () => Facts;
}

export const deviceProfileCommands = (facts: () => Facts): Command[] => [
    { name: "getDeviceProfile", handler: mojoHandler(() => ({ deviceInfo: deviceInfo(facts()) })) },
    { name: "getDeviceId", handler: mojoHandler(() => ({ deviceId: deviceIdOf(deviceInfo(facts())) })) },
];

export const createDeviceProfileService = (deps: DeviceProfileDeps) => (): OnDemandService =>
    serveOnDemand(deps)(SERVICE_NAME, deviceProfileCommands(deps.facts));
