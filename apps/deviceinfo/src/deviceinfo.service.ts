// The Device Info settings card.

import { createState, type State, type StateHolder, type Unsubscribe } from "@webos/api/helpers/create-state.ts";
import { createNavigation } from "@webos/api/services/navigation.service.ts";
import {
    createDeviceInfo, type DeviceFacts, type EraseKind,
} from "./luna/deviceinfo.ts";
import { LunaCallError, errorTextOf, type LunaService } from "@webos/api/infra/luna/service.ts";
import type { LaunchParams } from "@webos/api/infra/app/service.ts";

export type DeviceInfoScreen = "main" | "more" | "reset";

export interface ConfirmDialog {
    readonly kind: EraseKind;
    readonly title: string;
    readonly message: string;
    readonly confirm: string;
}

export interface DeviceInfoData {
    readonly screen: DeviceInfoScreen;
    readonly busy: boolean;
    readonly message: string;
    readonly deviceName: string;
    readonly device?: DeviceFacts | undefined;
    readonly confirm?: ConfirmDialog | undefined;
    readonly menuOpen: boolean;
}

export interface DeviceInfoService {
    getState(): State<DeviceInfoData>;
    onStateChange(listener: (state: State<DeviceInfoData>) => void): Unsubscribe;
    onShown(params?: LaunchParams): void;
    onHidden(): void;
    onBack(): boolean;
    dispose(): void;

    onDeviceName(value: string): void;
    onSaveDeviceName(): void;
    onOpenMore(): void;
    onOpenReset(): void;
    onAskErase(kind: EraseKind): void;
    onConfirmErase(value: string): void;
    onMenu(): void;
    onMenuChoice(value: string): void;
}

export const HELP_URL = "https://help.webosarchive.org/en-us/";

// HP fullerase-dialog / erase-scene wording (spec).
export const confirmOf = (kind: EraseKind): ConfirmDialog => {
    switch (kind) {
    case "reboot":
        return {
            kind,
            title: "Restart",
            message: "Shuts down and restarts the device.",
            confirm: "Restart",
        };
    case "shutdown":
        return {
            kind,
            title: "Shut Down",
            message: "Are you sure? Your device will be turned off and will not work until you turn it back on.",
            confirm: "Shut Down",
        };
    case "eraseApps":
        return {
            kind,
            title: "Erase Apps & Data",
            message: "Are you sure? Applications you installed and all application settings and data will be erased.",
            confirm: "Erase Apps & Data",
        };
    case "eraseUsb":
        return {
            kind,
            title: "Erase USB Drive",
            message: "Are you sure? All your personal files stored on the USB drive will be erased.",
            confirm: "Erase USB Drive",
        };
    case "eraseAll":
        return {
            kind,
            title: "Full Erase",
            message: "Are you sure? All your applications, data and personal files will be erased.",
            confirm: "Full Erase",
        };
    case "wipe":
        return {
            kind,
            title: "Secure Full Erase",
            message: "Are you sure? All your applications, data and personal files will be erased.",
            confirm: "Secure Full Erase",
        };
    }
};

export const createDeviceInfoService = (luna: LunaService): DeviceInfoService => {
    const api = createDeviceInfo(luna);
    const screens = createNavigation<DeviceInfoScreen>("main");
    const state: StateHolder<DeviceInfoData> = createState<DeviceInfoData>({
        name: "deviceinfo:main",
        data: {
            screen: "main",
            busy: false,
            message: "",
            deviceName: "",
            menuOpen: false,
        },
    });
    let gone = false;

    const show = (screen: DeviceInfoScreen) => {
        if (screens.now() !== screen)
            screens.open(screen);
        state.patch({ screen }, `deviceinfo:${screen}`);
    };

    screens.onChange(() => state.patch({ screen: screens.now() }, `deviceinfo:${screens.now()}`));

    const fail = (error: unknown) => {
        if (gone)
            return;
        const message = error instanceof LunaCallError ? errorTextOf(error.reply)
            : error instanceof Error ? error.message : String(error);
        state.patch({ busy: false, message, confirm: undefined });
    };

    const load = async () => {
        state.patch({ busy: true, message: "" });
        try {
            const [device, prefs] = await Promise.all([
                api.getDeviceProfile(),
                api.getPreferences(["deviceName"]),
            ]);
            if (gone)
                return;
            state.patch({
                busy: false,
                device,
                deviceName: typeof prefs.deviceName === "string" ? prefs.deviceName : "",
            });
        } catch (error) {
            fail(error);
        }
    };

    const runErase = (kind: EraseKind) => {
        state.patch({ busy: true, message: "", confirm: undefined });
        const op = kind === "reboot" ? api.machineReboot()
            : kind === "shutdown" ? api.machineOff()
            : api.erase(kind);
        // App does not wait for erase to finish beyond the call returning.
        void op.then(() => {
            if (!gone)
                state.patch({ busy: false });
        }).catch(fail);
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
            state.patch({ screen: screens.now(), confirm: undefined, message: "" },
                `deviceinfo:${screens.now()}`);
            return true;
        },

        dispose() {
            gone = true;
            state.clear();
        },

        onDeviceName(value) {
            state.patch({ deviceName: value, message: "" });
        },

        onSaveDeviceName() {
            const name = state.get().data.deviceName.trim();
            if (!name) {
                state.patch({ message: "Enter a device name." });
                return;
            }
            void api.setPreferences({ deviceName: name }).catch(fail);
        },

        onOpenMore() {
            show("more");
        },

        onOpenReset() {
            show("reset");
        },

        onAskErase(kind) {
            // Restart has no confirm dialog in HP beyond a banner; still confirm lightly.
            state.patch({ confirm: confirmOf(kind) });
        },

        onConfirmErase(value) {
            const dialog = state.get().data.confirm;
            state.patch({ confirm: undefined });
            if (!dialog || value !== "ok")
                return;
            runErase(dialog.kind);
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
