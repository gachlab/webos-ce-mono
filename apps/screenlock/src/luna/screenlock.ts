// Display, systemservice preferences, systemmanager lock, and wallpaper.
//
// Vocabulary matches HP's Screen & Lock card on the TouchPad CE image (spec,
// not code): enableALS, maximumBrightness, timeout, lockTimeout, lockMode.

import type { LunaService, Payload } from "@webos/api/infra/luna/service.ts";

export type LockMode = "none" | "pin" | "password";

export interface DisplayProps {
    readonly timeout: number;
    readonly maximumBrightness: number;
}

export interface ScreenPrefs {
    readonly enableALS: boolean;
    readonly sysUiEnableNextPrevGestures: boolean;
    readonly showAlertsWhenLocked: boolean;
    readonly BlinkNotifications: boolean;
    readonly lockTimeout: number;
    readonly wallpaper?: string;
}

export interface LockStatus {
    readonly lockMode: LockMode;
}

const text = (value: unknown): string => (typeof value === "string" ? value : "");
const num = (value: unknown, fallback = 0): number =>
    (typeof value === "number" && Number.isFinite(value) ? value : fallback);
const flag = (value: unknown, fallback = false): boolean =>
    (typeof value === "boolean" ? value : fallback);

const lockModeOf = (value: unknown): LockMode => {
    const name = text(value);
    if (name === "pin" || name === "password")
        return name;
    return "none";
};

export interface ScreenLockClient {
    getProperty(): Promise<DisplayProps>;
    setProperty(change: Partial<DisplayProps>): Promise<void>;
    getPreferences(keys: string[]): Promise<Partial<ScreenPrefs> & Payload>;
    setPreferences(change: Payload): Promise<void>;
    getDeviceLockMode(): Promise<LockStatus>;
    setDevicePasscode(passCode: string, lockMode: LockMode): Promise<void>;
    importWallpaper(target: string): Promise<void>;
    openTarget(target: string): Promise<void>;
}

export const createScreenLock = (luna: LunaService): ScreenLockClient => {
    const display = "luna://com.palm.display/control/";
    const system = "luna://com.palm.systemservice/";
    const manager = "luna://com.palm.systemmanager/";
    const wallpaper = "luna://com.palm.systemservice/wallpaper/";
    const apps = "luna://com.palm.applicationManager/";

    return {
        async getProperty() {
            const reply = await luna.call(`${display}getProperty`, {
                properties: ["timeout", "maximumBrightness"],
            });
            return {
                timeout: num(reply.timeout, 120),
                maximumBrightness: num(reply.maximumBrightness, 50),
            };
        },

        async setProperty(change) {
            await luna.call(`${display}setProperty`, change as Payload);
        },

        async getPreferences(keys) {
            return await luna.call(`${system}getPreferences`, { keys });
        },

        async setPreferences(change) {
            await luna.call(`${system}setPreferences`, change);
        },

        async getDeviceLockMode() {
            const reply = await luna.call(`${manager}getDeviceLockMode`, {});
            return { lockMode: lockModeOf(reply.lockMode) };
        },

        async setDevicePasscode(passCode, lockMode) {
            await luna.call(`${manager}setDevicePasscode`, { passCode, lockMode });
        },

        async importWallpaper(target) {
            const reply = await luna.call(`${wallpaper}importWallpaper`, { target });
            if (reply.wallpaper)
                await luna.call(`${system}setPreferences`, { wallpaper: reply.wallpaper });
        },

        async openTarget(target) {
            await luna.call(`${apps}open`, { target });
        },
    };
};
