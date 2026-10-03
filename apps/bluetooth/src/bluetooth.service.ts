// The Bluetooth settings card: the radio switch, the trusted and discovered
// devices, pairing (PIN, passkey, numeric comparison), and per-device connect.
//
// Screens and wording follow HP's bluetoothtab on the TouchPad CE image (spec,
// not code). The services are com.palm.btmonitor and com.palm.bluetooth from
// services/bluetooth on BlueZ.

import { createState, type State, type StateHolder, type Unsubscribe } from "@webos/api/helpers/create-state.ts";
import { createNavigation } from "@webos/api/services/navigation.service.ts";
import {
    createBluetooth, type Device, type PairingPrompt, type Profile, type Radio,
} from "./luna/bluetooth.ts";
import { LunaCallError, errorTextOf, type LunaService, type Subscription } from "@webos/api/infra/luna/service.ts";
import type { LaunchParams } from "@webos/api/infra/app/service.ts";

export type BtScreen = "list" | "details" | "pairing";

export interface BtData {
    readonly screen: BtScreen;
    readonly radio: Radio;
    readonly devices: Device[];
    readonly discovering: boolean;
    readonly caption: string;
    readonly busy: boolean;
    readonly message: string;
    readonly swipeOpen?: string | undefined;
    readonly details?: Device | undefined;
    // The pairing prompt on screen, and what the user has typed into it.
    readonly prompt?: PairingPrompt | undefined;
    readonly entry?: string | undefined;
}

export interface BtService {
    getState(): State<BtData>;
    onStateChange(listener: (state: State<BtData>) => void): Unsubscribe;
    onShown(params?: LaunchParams): void;
    onHidden(): void;
    onBack(): boolean;
    dispose(): void;

    onToggleRadio(): void;
    onToggleDiscovery(): void;
    onOpenDetails(address: string): void;
    onTapDevice(address: string): void;
    onSwipe(address: string, open: boolean): void;
    onForget(address: string): void;
    onConnectDisconnect(): void;
    onForgetDetails(): void;
    // Pairing prompt answers.
    onPromptEntry(value: string): void;
    onPromptAccept(): void;
    onPromptReject(): void;
}

// Whether the radio is in a settled on/off versus a transition. The switch is
// disabled while turning on or off, as HP's spinner shows.
export const radioOn = (radio: Radio): boolean => radio === "on" || radio === "turningon";
export const radioBusy = (radio: Radio): boolean => radio === "turningon" || radio === "turningoff";

// HP shows the uppercase line under a device only while it is moving.
export const statusLabel = (status: Device["status"]): string => {
    switch (status) {
    case "connecting": return "CONNECTING...";
    case "disconnecting": return "DISCONNECTING...";
    default: return "";
    }
};

// The device class says which profile row the card offers. A phone/headset is
// audio (a2dp/hfg); the card connects the whole device ("all") like the menu,
// and the detail screen is where a specific profile could be chosen later.
const deviceNamed = (devices: Device[], address: string): Device | undefined =>
    devices.find((d) => d.address === address);

export const createBtService = (luna: LunaService): BtService => {
    const bt = createBluetooth(luna);
    const screens = createNavigation<BtScreen>("list");
    const state: StateHolder<BtData> = createState<BtData>({
        name: "bt:list",
        data: {
            screen: "list",
            radio: "off",
            devices: [],
            discovering: false,
            caption: "",
            busy: false,
            message: "",
        },
    });
    let gone = false;
    const subscriptions: Subscription[] = [];

    const show = (screen: BtScreen) => {
        if (screens.now() !== screen)
            screens.open(screen);
        state.patch({ screen }, `bt:${screen}`);
    };

    screens.onChange(() => state.patch({ screen: screens.now() }, `bt:${screens.now()}`));

    const fail = (error: unknown) => {
        if (gone)
            return;
        const message = error instanceof LunaCallError ? errorTextOf(error.reply)
            : error instanceof Error ? error.message : String(error);
        state.patch({ busy: false, message });
    };

    const captionFor = (radio: Radio, devices: Device[]): string => {
        if (!radioOn(radio))
            return "";
        return devices.length === 0 ? "No devices" : "";
    };

    const openList = () => {
        while (screens.back())
            ;
        const current = state.get().data;
        state.set({
            name: "bt:list",
            data: {
                screen: "list",
                radio: current.radio,
                devices: current.devices,
                discovering: current.discovering,
                caption: captionFor(current.radio, current.devices),
                busy: false,
                message: "",
            },
        });
    };

    // Reject whatever pairing prompt is on screen, routing by kind: a
    // confirmation/authorization answers no; a passkey/PIN cancels the pairing
    // at BlueZ (which makes BlueZ call the agent's Cancel and return the held
    // call). Shared by the explicit reject button and the back gesture, so a
    // back-out never leaves the BlueZ call hanging.
    const rejectPrompt = (prompt: PairingPrompt) => {
        const op = prompt.kind === "requestconfirmation" || prompt.kind === "requestauthorization"
            ? bt.supplyConfirmation(false)
            : bt.cancelPairing(prompt.address);
        void op.catch(() => undefined);
    };

    const forget = (address: string) => {
        const device = deviceNamed(state.get().data.devices, address)
            ?? state.get().data.details;
        if (!device || state.get().data.busy)
            return;
        state.patch({ busy: true, message: "", swipeOpen: undefined });
        void bt.removeDevice(device.address).then(() => {
            if (!gone)
                openList();
        }).catch(fail);
    };

    return {
        getState: () => state.get(),
        onStateChange: (listener) => state.subscribe(listener),

        onShown() {
            gone = false;
            if (subscriptions.length > 0)
                return;
            subscriptions.push(bt.watchRadio((radio) => {
                if (gone)
                    return;
                const devices = state.get().data.devices;
                state.patch({ radio, caption: captionFor(radio, devices) });
            }));
            subscriptions.push(bt.watchGap(
                (devices) => {
                    if (gone)
                        return;
                    const details = state.get().data.details;
                    const updated = details ? deviceNamed(devices, details.address) : undefined;
                    state.patch({
                        devices,
                        caption: captionFor(state.get().data.radio, devices),
                        ...(updated ? { details: updated } : {}),
                    });
                },
                (prompt) => {
                    if (gone)
                        return;
                    // A prompt interrupts whatever is on screen: show it.
                    state.patch({ prompt, entry: "", message: "" });
                    show("pairing");
                }));
        },

        onHidden() {
            for (const sub of subscriptions)
                sub.cancel();
            subscriptions.length = 0;
        },

        onBack() {
            if (screens.now() === "list")
                return false;
            // Backing out of a pairing prompt rejects it, by kind -- a bare
            // supplyConfirmation(false) would be a no-op for a passkey/PIN
            // prompt and leave the BlueZ call hanging.
            if (screens.now() === "pairing" && state.get().data.prompt)
                rejectPrompt(state.get().data.prompt!);
            if (!screens.back())
                return false;
            if (screens.now() === "list") {
                openList();
                return true;
            }
            state.patch({ screen: screens.now(), busy: false, message: "" });
            return true;
        },

        dispose() {
            gone = true;
            for (const sub of subscriptions)
                sub.cancel();
            subscriptions.length = 0;
            state.clear();
        },

        onToggleRadio() {
            const radio = state.get().data.radio;
            if (radioBusy(radio) || state.get().data.busy)
                return;
            state.patch({ message: "" });
            void bt.setRadio(!radioOn(radio)).catch(fail);
        },

        onToggleDiscovery() {
            const data = state.get().data;
            if (!radioOn(data.radio))
                return;
            const op = data.discovering ? bt.stopDiscovery() : bt.startDiscovery();
            state.patch({ discovering: !data.discovering, message: "" });
            void op.catch(fail);
        },

        onOpenDetails(address) {
            const device = deviceNamed(state.get().data.devices, address);
            if (!device)
                return;
            state.patch({ details: device, message: "", swipeOpen: undefined });
            show("details");
        },

        // Tapping a device: pair it if it is not paired yet, otherwise connect
        // or disconnect -- which is what the menu row does.
        onTapDevice(address) {
            const device = deviceNamed(state.get().data.devices, address);
            if (!device || state.get().data.busy
                || device.status === "connecting" || device.status === "disconnecting")
                return;
            state.patch({ busy: true, message: "", swipeOpen: undefined });
            const op = !device.paired ? bt.pair(device.address)
                : device.status === "connected"
                    ? bt.disconnect(device.address, "all")
                    : bt.connect(device.address, "all");
            void op.then(() => {
                if (!gone)
                    state.patch({ busy: false });
            }).catch(fail);
        },

        onSwipe(address, open) {
            const device = deviceNamed(state.get().data.devices, address);
            if (device && (device.status === "connecting" || device.status === "disconnecting"))
                return;
            state.patch({ swipeOpen: open ? address : undefined });
        },

        onForget(address) {
            forget(address);
        },

        onConnectDisconnect() {
            const details = state.get().data.details;
            if (!details || state.get().data.busy)
                return;
            state.patch({ busy: true, message: "" });
            const op = details.status === "connected"
                ? bt.disconnect(details.address, "all")
                : bt.connect(details.address, "all");
            void op.then(() => {
                if (!gone)
                    state.patch({ busy: false });
            }).catch(fail);
        },

        onForgetDetails() {
            const details = state.get().data.details;
            if (details)
                forget(details.address);
        },

        onPromptEntry(value) {
            state.patch({ entry: value });
        },

        onPromptAccept() {
            const data = state.get().data;
            const prompt = data.prompt;
            if (!prompt)
                return;
            const done = () => {
                if (!gone)
                    state.patch({ prompt: undefined, entry: "", busy: false });
                if (!gone)
                    openList();
            };
            let op: Promise<void>;
            if (prompt.kind === "requestconfirmation" || prompt.kind === "requestauthorization") {
                op = bt.supplyConfirmation(true);
            } else if (prompt.kind === "requestpasskey") {
                op = bt.supplyPasskey(data.entry ?? "");
            } else if (prompt.kind === "requestpincode") {
                op = bt.supplyPinCode(data.entry ?? "");
            } else {
                // Display-only prompts (displaypasskey/displaypincode) have no
                // answer to send; acknowledging just dismisses them.
                done();
                return;
            }
            state.patch({ busy: true, message: "" });
            void op.then(done).catch(fail);
        },

        onPromptReject() {
            const prompt = state.get().data.prompt;
            if (!prompt)
                return;
            rejectPrompt(prompt);
            state.patch({ prompt: undefined, entry: "" });
            openList();
        },
    };
};
