// The Screen & Lock settings card: brightness, timeout, wallpaper, gestures,
// secure unlock, and notifications.
//
// Screens and wording follow HP's Screen & Lock card on the TouchPad CE image
// (spec, not code).

import { createState, type State, type StateHolder, type Unsubscribe } from "@webos/api/helpers/create-state.ts";
import { createNavigation } from "@webos/api/services/navigation.service.ts";
import {
    createScreenLock, type LockMode, type ScreenLockClient,
} from "./luna/screenlock.ts";
import { LunaCallError, errorTextOf, type LunaService } from "@webos/api/infra/luna/service.ts";
import type { LaunchParams } from "@webos/api/infra/app/service.ts";

export type ScreenLockScreen = "main" | "configure";

export interface ScreenLockData {
    readonly screen: ScreenLockScreen;
    readonly busy: boolean;
    readonly message: string;
    readonly enableALS: boolean;
    readonly brightness: number;
    readonly timeout: number;
    readonly gestures: boolean;
    readonly lockMode: LockMode;
    readonly lockTimeout: number;
    readonly showWhenLocked: boolean;
    readonly blink: boolean;
    readonly wallpaper: string;
    readonly choosingTimeout?: boolean | undefined;
    readonly choosingLock?: boolean | undefined;
    readonly choosingLockAfter?: boolean | undefined;
    readonly configureMode?: LockMode | undefined;
    readonly passcode: string;
    readonly passcodeConfirm: string;
    readonly menuOpen: boolean;
}

export interface ScreenLockService {
    getState(): State<ScreenLockData>;
    onStateChange(listener: (state: State<ScreenLockData>) => void): Unsubscribe;
    onShown(params?: LaunchParams): void;
    onHidden(): void;
    onBack(): boolean;
    dispose(): void;

    onAutoDim(on: boolean): void;
    onBrightness(value: number): void;
    onTimeout(seconds: number): void;
    onChooseTimeout(open: boolean): void;
    onChangeWallpaper(): void;
    onGestures(on: boolean): void;
    onLockMode(mode: LockMode): void;
    onChooseLock(open: boolean): void;
    onLockAfter(seconds: number): void;
    onChooseLockAfter(open: boolean): void;
    onShowWhenLocked(on: boolean): void;
    onBlink(on: boolean): void;
    onConfigureField(change: Partial<{ passcode: string; passcodeConfirm: string }>): void;
    onSaveConfigure(): void;
    onCancelConfigure(): void;
    onMenu(): void;
    onMenuChoice(value: string): void;
}

// HP's Turn off After captions for the display timeout.
export const timeoutLabel = (seconds: number): string => {
    switch (seconds) {
    case 60: return "1 minute";
    case 120: return "2 minutes";
    case 300: return "5 minutes";
    case 600: return "10 minutes";
    default: return `${seconds} seconds`;
    }
};

export const TIMEOUT_CHOICES = [
    { value: "60", label: "1 minute" },
    { value: "120", label: "2 minutes" },
    { value: "300", label: "5 minutes" },
    { value: "600", label: "10 minutes" },
];

export const LOCK_MODE_CHOICES = [
    { value: "none", label: "Off" },
    { value: "pin", label: "Simple PIN" },
    { value: "password", label: "Password" },
];

export const LOCK_AFTER_CHOICES = [
    { value: "0", label: "Screen turns off" },
    { value: "30", label: "30 seconds" },
    { value: "60", label: "1 minute" },
    { value: "120", label: "2 minutes" },
    { value: "180", label: "3 minutes" },
    { value: "300", label: "5 minutes" },
    { value: "600", label: "10 minutes" },
    { value: "1800", label: "30 minutes" },
];

export const HELP_URL = "https://help.webosarchive.org/en-us/";

const initial = (): ScreenLockData => ({
    screen: "main",
    busy: false,
    message: "",
    enableALS: true,
    brightness: 50,
    timeout: 120,
    gestures: true,
    lockMode: "none",
    lockTimeout: 0,
    showWhenLocked: false,
    blink: false,
    wallpaper: "",
    passcode: "",
    passcodeConfirm: "",
    menuOpen: false,
});

export const createScreenLockService = (luna: LunaService): ScreenLockService => {
    const api: ScreenLockClient = createScreenLock(luna);
    const screens = createNavigation<ScreenLockScreen>("main");
    const state: StateHolder<ScreenLockData> = createState<ScreenLockData>({
        name: "screenlock:main",
        data: initial(),
    });
    let gone = false;

    const show = (screen: ScreenLockScreen) => {
        if (screens.now() !== screen)
            screens.open(screen);
        state.patch({ screen }, `screenlock:${screen}`);
    };

    screens.onChange(() => state.patch({ screen: screens.now() }, `screenlock:${screens.now()}`));

    const fail = (error: unknown) => {
        if (gone)
            return;
        const message = error instanceof LunaCallError ? errorTextOf(error.reply)
            : error instanceof Error ? error.message : String(error);
        state.patch({ busy: false, message });
    };

    const load = async () => {
        state.patch({ busy: true, message: "" });
        try {
            const props = await api.getProperty();
            if (gone)
                return;
            const prefs = await api.getPreferences([
                "showAlertsWhenLocked", "sysUiEnableNextPrevGestures",
                "BlinkNotifications", "enableALS", "lockTimeout", "wallpaper",
            ]);
            if (gone)
                return;
            const lock = await api.getDeviceLockMode();
            if (gone)
                return;
            const wallpaper = typeof prefs.wallpaper === "string" ? prefs.wallpaper
                : typeof prefs.wallpaper === "object" && prefs.wallpaper
                    ? String((prefs.wallpaper as { wallpaperFile?: string }).wallpaperFile ?? "")
                    : "";
            state.patch({
                busy: false,
                timeout: props.timeout,
                brightness: props.maximumBrightness,
                enableALS: typeof prefs.enableALS === "boolean" ? prefs.enableALS : true,
                gestures: typeof prefs.sysUiEnableNextPrevGestures === "boolean"
                    ? prefs.sysUiEnableNextPrevGestures : true,
                showWhenLocked: typeof prefs.showAlertsWhenLocked === "boolean"
                    ? prefs.showAlertsWhenLocked : false,
                blink: typeof prefs.BlinkNotifications === "boolean"
                    ? prefs.BlinkNotifications : false,
                lockTimeout: typeof prefs.lockTimeout === "number" ? prefs.lockTimeout : 0,
                lockMode: lock.lockMode,
                wallpaper,
            });
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
        },

        onHidden() {},

        onBack() {
            if (screens.now() === "main")
                return false;
            if (!screens.back())
                return false;
            state.patch({
                screen: "main",
                configureMode: undefined,
                passcode: "",
                passcodeConfirm: "",
                message: "",
            }, "screenlock:main");
            return true;
        },

        dispose() {
            gone = true;
            state.clear();
        },

        onAutoDim(on) {
            state.patch({ enableALS: on, message: "" });
            void api.setPreferences({ enableALS: on }).catch(fail);
        },

        onBrightness(value) {
            state.patch({ brightness: value, message: "" });
            void api.setProperty({ maximumBrightness: value }).catch(fail);
        },

        onTimeout(seconds) {
            state.patch({ timeout: seconds, choosingTimeout: false, message: "" });
            void api.setProperty({ timeout: seconds }).catch(fail);
        },

        onChooseTimeout(open) {
            state.patch({ choosingTimeout: open });
        },

        onChangeWallpaper() {
            state.patch({ message: "" });
            // Prefer opening Photos / a file picker target; if that fails, say so.
            void api.openTarget("file:///media/internal/")
                .then(() => {
                    if (!gone)
                        state.patch({ message: "Pick an image, then return here." });
                })
                .catch(() => {
                    if (!gone)
                        state.patch({
                            message: wallpaperNote(state.get().data.wallpaper),
                        });
                });
        },

        onGestures(on) {
            state.patch({ gestures: on, message: "" });
            void api.setPreferences({ sysUiEnableNextPrevGestures: on }).catch(fail);
        },

        onLockMode(mode) {
            state.patch({ choosingLock: false, message: "" });
            if (mode === "none") {
                state.patch({ lockMode: "none" });
                void api.setDevicePasscode("", "none").catch(fail);
                return;
            }
            state.patch({
                configureMode: mode,
                passcode: "",
                passcodeConfirm: "",
            });
            show("configure");
        },

        onChooseLock(open) {
            state.patch({ choosingLock: open });
        },

        onLockAfter(seconds) {
            state.patch({ lockTimeout: seconds, choosingLockAfter: false, message: "" });
            void api.setPreferences({ lockTimeout: seconds }).catch(fail);
        },

        onChooseLockAfter(open) {
            state.patch({ choosingLockAfter: open });
        },

        onShowWhenLocked(on) {
            state.patch({ showWhenLocked: on, message: "" });
            void api.setPreferences({ showAlertsWhenLocked: on }).catch(fail);
        },

        onBlink(on) {
            state.patch({ blink: on, message: "" });
            void api.setPreferences({ BlinkNotifications: on }).catch(fail);
        },

        onConfigureField(change) {
            state.patch({ ...change, message: "" });
        },

        onSaveConfigure() {
            const data = state.get().data;
            const mode = data.configureMode;
            if (!mode || mode === "none")
                return;
            if (!data.passcode) {
                state.patch({ message: mode === "pin" ? "Enter a PIN." : "Enter a password." });
                return;
            }
            if (data.passcode !== data.passcodeConfirm) {
                state.patch({ message: "The entries do not match." });
                return;
            }
            if (mode === "pin" && !/^\d{4,}$/.test(data.passcode)) {
                state.patch({ message: "PIN must be at least 4 digits." });
                return;
            }
            state.patch({ busy: true, message: "" });
            void api.setDevicePasscode(data.passcode, mode).then(() => {
                if (gone)
                    return;
                while (screens.back())
                    ;
                state.patch({
                    screen: "main",
                    lockMode: mode,
                    busy: false,
                    configureMode: undefined,
                    passcode: "",
                    passcodeConfirm: "",
                }, "screenlock:main");
            }).catch(fail);
        },

        onCancelConfigure() {
            while (screens.back())
                ;
            state.patch({
                screen: "main",
                configureMode: undefined,
                passcode: "",
                passcodeConfirm: "",
                message: "",
            }, "screenlock:main");
        },

        onMenu() {
            state.patch({ menuOpen: true });
        },

        onMenuChoice(value) {
            state.patch({ menuOpen: false });
            if (value === "help") {
                void api.openTarget(HELP_URL).catch(fail);
            }
        },
    };
};

const wallpaperNote = (current: string): string =>
    (current
        ? "Could not open a picker. Current wallpaper is set."
        : "Could not open a picker. Set wallpaper from Photos when available.");
