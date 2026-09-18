// applicationManager listApps + appinstaller remove.

import type { LunaService, Payload } from "@webos/api/infra/luna/service.ts";

export interface InstalledApp {
    readonly id: string;
    readonly title: string;
    readonly version: string;
    readonly vendor: string;
    readonly removable: boolean;
}

export interface SwManagerClient {
    listApps(): Promise<InstalledApp[]>;
    remove(id: string): Promise<void>;
    openTarget(target: string): Promise<void>;
}

const text = (value: unknown): string => (typeof value === "string" ? value : "");

const removableOf = (value: unknown): boolean => {
    if (typeof value === "boolean")
        return value;
    if (value === "false" || value === "0")
        return false;
    if (value === "true" || value === "1")
        return true;
    // HP defaults removable apps to true when the flag is omitted.
    return true;
};

export const appOf = (raw: Payload): InstalledApp => ({
    id: text(raw.id),
    title: text(raw.title) || text(raw.id) || "App",
    version: text(raw.version),
    vendor: text(raw.vendor),
    removable: removableOf(raw.removable),
});

export const createSwManager = (luna: LunaService): SwManagerClient => {
    const apps = "luna://com.palm.applicationManager/";
    const installer = "luna://com.palm.appinstaller/";

    return {
        async listApps() {
            const reply = await luna.call(`${apps}listApps`, {});
            const list = Array.isArray(reply.apps) ? reply.apps : [];
            return list.map((item) => appOf(item as Payload))
                .filter((app) => !!app.id)
                .sort((a, b) => a.title.localeCompare(b.title));
        },

        async remove(id) {
            await luna.call(`${installer}remove`, { id });
        },

        async openTarget(target) {
            await luna.call(`${apps}open`, { target });
        },
    };
};
