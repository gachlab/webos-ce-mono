// com.palm.systemservice time preferences and setSystemTime.

import type { LunaService, Payload } from "@webos/api/infra/luna/service.ts";

export type TimeFormat = "HH12" | "HH24";

export interface TimeZoneEntry {
    readonly Country: string;
    readonly City: string;
    readonly Description: string;
    readonly ZoneID?: string;
    readonly raw: Payload;
}

export interface DateTimeClient {
    getPreferences(keys: string[]): Promise<Payload>;
    setPreferences(change: Payload): Promise<void>;
    getPreferenceValues(key: string): Promise<TimeZoneEntry[]>;
    setSystemTime(utcSeconds: number): Promise<void>;
    openTarget(target: string): Promise<void>;
}

const text = (value: unknown): string => (typeof value === "string" ? value : "");

const zoneOf = (raw: Payload): TimeZoneEntry => ({
    Country: text(raw.Country),
    City: text(raw.City),
    Description: text(raw.Description),
    ...(text(raw.ZoneID) ? { ZoneID: text(raw.ZoneID) } : {}),
    raw,
});

export const createDateTime = (luna: LunaService): DateTimeClient => {
    const system = "luna://com.palm.systemservice/";
    const time = "luna://com.palm.systemservice/time/";
    const apps = "luna://com.palm.applicationManager/";

    return {
        async getPreferences(keys) {
            return await luna.call(`${system}getPreferences`, { keys });
        },

        async setPreferences(change) {
            await luna.call(`${system}setPreferences`, change);
        },

        async getPreferenceValues(key) {
            const reply = await luna.call(`${system}getPreferenceValues`, { key });
            const list = Array.isArray(reply.timeZone) ? reply.timeZone
                : Array.isArray(reply[key]) ? reply[key] as unknown[]
                : [];
            return list.map((item) => zoneOf(item as Payload));
        },

        async setSystemTime(utcSeconds) {
            await luna.call(`${time}setSystemTime`, { utc: utcSeconds });
        },

        async openTarget(target) {
            await luna.call(`${apps}open`, { target });
        },
    };
};

export const formatClock = (date: Date, format: TimeFormat): string => {
    const pad = (n: number) => String(n).padStart(2, "0");
    const y = date.getFullYear();
    const m = pad(date.getMonth() + 1);
    const d = pad(date.getDate());
    if (format === "HH24") {
        return `${y}-${m}-${d} ${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;
    }
    const h24 = date.getHours();
    const am = h24 < 12;
    const h12 = h24 % 12 || 12;
    return `${y}-${m}-${d} ${h12}:${pad(date.getMinutes())}:${pad(date.getSeconds())} ${am ? "AM" : "PM"}`;
};

export const zoneLabel = (zone: TimeZoneEntry | undefined): string => {
    if (!zone)
        return "";
    if (zone.Country && zone.City)
        return `${zone.Country} ${zone.City}`;
    return zone.Description || zone.City || zone.Country;
};
