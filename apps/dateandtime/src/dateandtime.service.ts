// The Date & Time settings card.

import { createState, type State, type StateHolder, type Unsubscribe } from "@webos/api/helpers/create-state.ts";
import { createNavigation } from "@webos/api/services/navigation.service.ts";
import {
    createDateTime, formatClock, zoneLabel, type TimeFormat, type TimeZoneEntry,
} from "./luna/dateandtime.ts";
import { LunaCallError, errorTextOf, type LunaService, type Payload } from "@webos/api/infra/luna/service.ts";
import type { LaunchParams } from "@webos/api/infra/app/service.ts";

export type DateTimeScreen = "main" | "timezone";

export interface DateTimeData {
    readonly screen: DateTimeScreen;
    readonly busy: boolean;
    readonly message: string;
    readonly timeFormat: TimeFormat;
    readonly useNetworkTime: boolean;
    readonly useNetworkTimeZone: boolean;
    readonly nowLabel: string;
    readonly dateValue: string;
    readonly timeValue: string;
    readonly timeZone?: TimeZoneEntry | undefined;
    readonly zones: TimeZoneEntry[];
    readonly zoneFilter: string;
    readonly choosingFormat?: boolean | undefined;
    readonly menuOpen: boolean;
}

export interface DateTimeDeps {
    readonly luna: LunaService;
    readonly setInterval: (callback: () => void, ms: number) => unknown;
    readonly clearInterval: (handle: unknown) => void;
    readonly now?: () => Date;
}

export interface DateTimeService {
    getState(): State<DateTimeData>;
    onStateChange(listener: (state: State<DateTimeData>) => void): Unsubscribe;
    onShown(params?: LaunchParams): void;
    onHidden(): void;
    onBack(): boolean;
    dispose(): void;

    onTimeFormat(format: TimeFormat): void;
    onChooseFormat(open: boolean): void;
    onNetworkTime(on: boolean): void;
    onNetworkTimeZone(on: boolean): void;
    onDateValue(value: string): void;
    onTimeValue(value: string): void;
    onApplyManualTime(): void;
    onOpenTimezone(): void;
    onZoneFilter(text: string): void;
    onPickTimezone(index: number): void;
    onCancelTimezone(): void;
    onMenu(): void;
    onMenuChoice(value: string): void;
}

export const HELP_URL = "https://help.webosarchive.org/en-us/";

export const FORMAT_CHOICES = [
    { value: "HH12", label: "12 hour" },
    { value: "HH24", label: "24 hour" },
];

export const filterZones = (zones: TimeZoneEntry[], filter: string): TimeZoneEntry[] => {
    const needle = filter.trim().toLowerCase();
    if (!needle)
        return zones;
    return zones.filter((z) =>
        zoneLabel(z).toLowerCase().includes(needle)
        || z.Description.toLowerCase().includes(needle));
};

const zoneFromPref = (value: unknown): TimeZoneEntry | undefined => {
    if (!value || typeof value !== "object")
        return undefined;
    const raw = value as Payload;
    return {
        Country: typeof raw.Country === "string" ? raw.Country : "",
        City: typeof raw.City === "string" ? raw.City : "",
        Description: typeof raw.Description === "string" ? raw.Description : "",
        ...(typeof raw.ZoneID === "string" ? { ZoneID: raw.ZoneID } : {}),
        raw,
    };
};

const toLocalInputs = (date: Date): { dateValue: string; timeValue: string } => {
    const pad = (n: number) => String(n).padStart(2, "0");
    return {
        dateValue: `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}`,
        timeValue: `${pad(date.getHours())}:${pad(date.getMinutes())}`,
    };
};

export const createDateTimeService = (deps: DateTimeDeps): DateTimeService => {
    const api = createDateTime(deps.luna);
    const clock = deps.now ?? (() => new Date());
    const screens = createNavigation<DateTimeScreen>("main");
    const state: StateHolder<DateTimeData> = createState<DateTimeData>({
        name: "dateandtime:main",
        data: {
            screen: "main",
            busy: false,
            message: "",
            timeFormat: "HH12",
            useNetworkTime: true,
            useNetworkTimeZone: true,
            nowLabel: "",
            dateValue: "",
            timeValue: "",
            zones: [],
            zoneFilter: "",
            menuOpen: false,
        },
    });
    let gone = false;
    let tick: unknown;

    const show = (screen: DateTimeScreen) => {
        if (screens.now() !== screen)
            screens.open(screen);
        state.patch({ screen }, `dateandtime:${screen}`);
    };

    screens.onChange(() => state.patch({ screen: screens.now() }, `dateandtime:${screens.now()}`));

    const fail = (error: unknown) => {
        if (gone)
            return;
        const message = error instanceof LunaCallError ? errorTextOf(error.reply)
            : error instanceof Error ? error.message : String(error);
        state.patch({ busy: false, message });
    };

    const paintNow = () => {
        if (gone)
            return;
        const date = clock();
        const data = state.get().data;
        const inputs = data.useNetworkTime ? toLocalInputs(date) : {
            dateValue: data.dateValue || toLocalInputs(date).dateValue,
            timeValue: data.timeValue || toLocalInputs(date).timeValue,
        };
        state.patch({
            nowLabel: formatClock(date, data.timeFormat),
            ...inputs,
        });
    };

    const load = async () => {
        state.patch({ busy: true, message: "" });
        try {
            const prefs = await api.getPreferences([
                "timeFormat", "timeZone", "useNetworkTime", "useNetworkTimeZone",
            ]);
            if (gone)
                return;
            const format = prefs.timeFormat === "HH24" ? "HH24" : "HH12";
            const zone = zoneFromPref(prefs.timeZone);
            state.patch({
                busy: false,
                timeFormat: format,
                useNetworkTime: typeof prefs.useNetworkTime === "boolean"
                    ? prefs.useNetworkTime : true,
                useNetworkTimeZone: typeof prefs.useNetworkTimeZone === "boolean"
                    ? prefs.useNetworkTimeZone : true,
                ...(zone ? { timeZone: zone } : {}),
            });
            paintNow();
        } catch (error) {
            fail(error);
        }
    };

    return {
        getState: () => state.get(),
        onStateChange: (listener) => state.subscribe(listener),

        onShown() {
            gone = false;
            void load();
            if (tick === undefined)
                tick = deps.setInterval(paintNow, 1000);
        },

        onHidden() {
            if (tick !== undefined) {
                deps.clearInterval(tick);
                tick = undefined;
            }
        },

        onBack() {
            if (screens.now() === "main")
                return false;
            if (!screens.back())
                return false;
            state.patch({ screen: "main", zoneFilter: "", message: "" }, "dateandtime:main");
            return true;
        },

        dispose() {
            gone = true;
            if (tick !== undefined) {
                deps.clearInterval(tick);
                tick = undefined;
            }
            state.clear();
        },

        onTimeFormat(format) {
            state.patch({ timeFormat: format, choosingFormat: false, message: "" });
            paintNow();
            void api.setPreferences({ timeFormat: format }).catch(fail);
        },

        onChooseFormat(open) {
            state.patch({ choosingFormat: open });
        },

        onNetworkTime(on) {
            state.patch({ useNetworkTime: on, message: "" });
            paintNow();
            void api.setPreferences({ useNetworkTime: on }).catch(fail);
        },

        onNetworkTimeZone(on) {
            state.patch({ useNetworkTimeZone: on, message: "" });
            void api.setPreferences({ useNetworkTimeZone: on }).catch(fail);
        },

        onDateValue(value) {
            state.patch({ dateValue: value, message: "" });
        },

        onTimeValue(value) {
            state.patch({ timeValue: value, message: "" });
        },

        onApplyManualTime() {
            const data = state.get().data;
            const stamp = Date.parse(`${data.dateValue}T${data.timeValue}:00`);
            if (!Number.isFinite(stamp)) {
                state.patch({ message: "Enter a valid date and time." });
                return;
            }
            state.patch({ busy: true, message: "" });
            void api.setSystemTime(Math.floor(stamp / 1000)).then(() => {
                if (gone)
                    return;
                void api.setPreferences({ receiveNetworkTimeUpdate: false }).catch(fail);
                state.patch({ busy: false });
                paintNow();
            }).catch(fail);
        },

        onOpenTimezone() {
            state.patch({ busy: true, message: "", zoneFilter: "" });
            show("timezone");
            void api.getPreferenceValues("timeZone").then((zones) => {
                if (!gone)
                    state.patch({ zones, busy: false });
            }).catch(() => {
                // HP falls back to timeZone1 on some builds.
                void api.getPreferenceValues("timeZone1").then((zones) => {
                    if (!gone)
                        state.patch({ zones, busy: false });
                }).catch(fail);
            });
        },

        onZoneFilter(text) {
            state.patch({ zoneFilter: text });
        },

        onPickTimezone(index) {
            const visible = filterZones(state.get().data.zones, state.get().data.zoneFilter);
            const picked = visible[index];
            if (!picked)
                return;
            state.patch({ busy: true, message: "" });
            void api.setPreferences({
                timeZone: picked.raw,
                receiveNetworkTimezoneUpdate: false,
            }).then(() => {
                if (gone)
                    return;
                while (screens.back())
                    ;
                state.patch({
                    screen: "main",
                    timeZone: picked,
                    busy: false,
                    zoneFilter: "",
                }, "dateandtime:main");
            }).catch(fail);
        },

        onCancelTimezone() {
            while (screens.back())
                ;
            state.patch({ screen: "main", zoneFilter: "", message: "" }, "dateandtime:main");
        },

        onMenu() {
            state.patch({ menuOpen: true });
        },

        onMenuChoice(value) {
            state.patch({ menuOpen: false });
            if (value === "help")
                void api.openTarget(HELP_URL).catch(fail);
        },
    };
};
