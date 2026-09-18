// Device profile, power, and storage erase — HP Device Info vocabulary.

import type { LunaService, Payload } from "@webos/api/infra/luna/service.ts";

export type EraseKind =
    | "reboot"
    | "shutdown"
    | "eraseApps"
    | "eraseUsb"
    | "eraseAll"
    | "wipe";

export interface DeviceFacts {
    readonly model: string;
    readonly version: string;
    readonly serial: string;
}

export interface DeviceInfoClient {
    getDeviceProfile(): Promise<DeviceFacts>;
    getPreferences(keys: string[]): Promise<Payload>;
    setPreferences(change: Payload): Promise<void>;
    machineReboot(): Promise<void>;
    machineOff(): Promise<void>;
    erase(kind: Exclude<EraseKind, "reboot" | "shutdown">): Promise<void>;
    openTarget(target: string): Promise<void>;
}

const text = (value: unknown): string => (typeof value === "string" ? value : "");

export const factsOf = (reply: Payload): DeviceFacts => {
    const info = (reply.deviceInfo ?? {}) as Payload;
    return {
        model: text(info.deviceModel) || "webOS device",
        version: text(info.softwareVersion) || text(info.carrierROM),
        serial: text(info.nduId),
    };
};

// Maps the card's erase kind to the storaged / power method HP used.
export const eraseMethodOf = (kind: EraseKind): { uri: string; method: string } => {
    switch (kind) {
    case "reboot":
        return { uri: "luna://com.palm.power/shutdown/", method: "machineReboot" };
    case "shutdown":
        return { uri: "luna://com.palm.power/shutdown/", method: "machineOff" };
    case "eraseApps":
        return { uri: "luna://com.palm.storage/erase/", method: "EraseVar" };
    case "eraseUsb":
        return { uri: "luna://com.palm.storage/erase/", method: "EraseMedia" };
    case "eraseAll":
        return { uri: "luna://com.palm.storage/erase/", method: "EraseAll" };
    case "wipe":
        return { uri: "luna://com.palm.storage/erase/", method: "Wipe" };
    }
};

export const createDeviceInfo = (luna: LunaService): DeviceInfoClient => {
    const system = "luna://com.palm.systemservice/";
    const apps = "luna://com.palm.applicationManager/";

    return {
        async getDeviceProfile() {
            const reply = await luna.call("luna://com.palm.deviceprofile/getDeviceProfile", {});
            return factsOf(reply);
        },

        async getPreferences(keys) {
            return await luna.call(`${system}getPreferences`, { keys });
        },

        async setPreferences(change) {
            await luna.call(`${system}setPreferences`, change);
        },

        async machineReboot() {
            const { uri, method } = eraseMethodOf("reboot");
            await luna.call(`${uri}${method}`, { reason: "User initiated/DeviceInfo" });
        },

        async machineOff() {
            const { uri, method } = eraseMethodOf("shutdown");
            await luna.call(`${uri}${method}`, { reason: "User initiated/DeviceInfo" });
        },

        async erase(kind) {
            const { uri, method } = eraseMethodOf(kind);
            await luna.call(`${uri}${method}`, {});
        },

        async openTarget(target) {
            await luna.call(`${apps}open`, { target });
        },
    };
};
