// com.palm.btmonitor and com.palm.bluetooth, typed.
//
// Vocabulary matches what HP's system menu drawer and the service expect
// (reference/luna-sysmgr-ce's StatusBarServicesConnector): the "radio" strings,
// the trusteddevices list with address/name/status/cod, the profgetstate
// arrays, and the notifn* events. The modern fields the service adds -- battery,
// icon, addressType, and the pairing prompts -- are typed here too, since the
// card is their only reader. The service behind both names is ours
// (services/bluetooth on BlueZ).

import type { LunaService, Payload, Subscription } from "@webos/api/infra/luna/service.ts";

export type Radio = "on" | "turningon" | "turningoff" | "off";

export type ConnectionStatus = "connected" | "connecting" | "disconnecting" | "disconnected";

// HP's seven profile names; the card lists hfg/a2dp/hf/mapc like the menu.
export type Profile = "hfg" | "a2dp" | "pan" | "hid" | "spp" | "hf" | "mapc";

// A trusted or discovered device, as gettrusteddevices and the gap notifications
// describe it. battery/icon/addressType are the service's modern additions and
// are optional, since an older reply or a device without them omits them.
export interface Device {
    readonly address: string;
    readonly name: string;
    readonly status: ConnectionStatus;
    readonly cod: number;
    readonly paired: boolean;
    readonly battery?: number;
    readonly icon?: string;
    readonly addressType?: string;
}

// The pairing agent is asking the user something. kind is the Agent1 method,
// as the service's PromptKind strings; the card renders a screen per kind.
export type PromptKind =
    | "requestpincode" | "requestpasskey"
    | "displaypincode" | "displaypasskey"
    | "requestconfirmation" | "requestauthorization";

export interface PairingPrompt {
    readonly kind: PromptKind;
    readonly address: string;
    readonly name: string;
    readonly passkey?: string;
    readonly pincode?: string;
}

const text = (value: unknown): string => (typeof value === "string" ? value : "");
const num = (value: unknown): number => (typeof value === "number" ? value : 0);
const flag = (value: unknown): boolean => value === true;

const statusOf = (value: unknown): ConnectionStatus => {
    const name = text(value);
    if (name === "connected" || name === "connecting" || name === "disconnecting")
        return name;
    return "disconnected";
};

const deviceOf = (raw: Payload): Device => ({
    address: text(raw.address),
    name: text(raw.name) || text(raw.address),
    status: statusOf(raw.status),
    cod: num(raw.cod),
    paired: flag(raw.paired),
    ...(typeof raw.battery === "number" ? { battery: raw.battery } : {}),
    ...(raw.icon ? { icon: text(raw.icon) } : {}),
    ...(raw.addressType ? { addressType: text(raw.addressType) } : {}),
});

// A notification payload is a pairing prompt when it carries our
// notifnpairingrequest and a prompt kind.
const promptOf = (raw: Payload): PairingPrompt | undefined => {
    if (text(raw.notification) !== "notifnpairingrequest")
        return undefined;
    const kind = text(raw.prompt) as PromptKind;
    return {
        kind,
        address: text(raw.address),
        name: text(raw.name) || text(raw.address),
        ...(raw.passkey ? { passkey: text(raw.passkey) } : {}),
        ...(raw.pincode ? { pincode: text(raw.pincode) } : {}),
    };
};

export interface BluetoothClient {
    watchRadio(onRadio: (radio: Radio) => void): Subscription;
    // One subscription to gap/subscribenotifications feeds both the device list
    // and the pairing prompts -- the service pushes both on that one channel, so
    // the card opens it once and routes by payload shape.
    watchGap(onDevices: (devices: Device[]) => void,
             onPrompt: (prompt: PairingPrompt) => void): Subscription;
    setRadio(on: boolean): Promise<void>;
    startDiscovery(transport?: string): Promise<void>;
    stopDiscovery(): Promise<void>;
    pair(address: string): Promise<void>;
    cancelPairing(address: string): Promise<void>;
    removeDevice(address: string): Promise<void>;
    connect(address: string, profile: Profile | "all"): Promise<void>;
    disconnect(address: string, profile: Profile | "all"): Promise<void>;
    supplyConfirmation(accept: boolean): Promise<void>;
    supplyPasskey(passkey: string): Promise<void>;
    supplyPinCode(pincode: string): Promise<void>;
}

export const createBluetooth = (luna: LunaService): BluetoothClient => {
    const monitor = "luna://com.palm.btmonitor/monitor/";
    const gap = "luna://com.palm.bluetooth/gap/";
    const prof = "luna://com.palm.bluetooth/prof/";

    const devicesFrom = (payload: Payload): Device[] => {
        const list = Array.isArray(payload.trusteddevices) ? payload.trusteddevices : [];
        return list.map((item) => deviceOf(item as Payload));
    };

    return {
        watchRadio(onRadio) {
            return luna.subscribe(`${monitor}subscribenotifications`, { subscribe: true },
                (payload) => {
                    const radio = text(payload.radio);
                    if (radio === "on" || radio === "turningon"
                        || radio === "turningoff" || radio === "off")
                        onRadio(radio);
                });
        },

        watchGap(onDevices, onPrompt) {
            // gap/subscribenotifications carries two kinds of push: the trusted
            // device list (whole list, which the service re-pushes on any
            // change) and a pairing prompt (notifnpairingrequest). One
            // subscription, routed by payload shape.
            return luna.subscribe(`${gap}subscribenotifications`, { subscribe: true },
                (payload) => {
                    if (Array.isArray(payload.trusteddevices)) {
                        onDevices(devicesFrom(payload));
                        return;
                    }
                    const prompt = promptOf(payload);
                    if (prompt)
                        onPrompt(prompt);
                });
        },

        async setRadio(on) {
            await luna.call(on ? `${monitor}radioon` : `${monitor}radiooff`,
                on ? { visible: false, connectable: true } : {});
        },

        async startDiscovery(transport) {
            await luna.call(`${gap}startdiscovery`, transport ? { transport } : {});
        },

        async stopDiscovery() {
            await luna.call(`${gap}stopdiscovery`, {});
        },

        async pair(address) {
            await luna.call(`${gap}pair`, { address });
        },

        async cancelPairing(address) {
            await luna.call(`${gap}cancelpairing`, { address });
        },

        async removeDevice(address) {
            await luna.call(`${gap}removedevice`, { address });
        },

        async connect(address, profile) {
            await luna.call(`${prof}profconnect`, { address, profile });
        },

        async disconnect(address, profile) {
            await luna.call(`${prof}profdisconnect`, { address, profile });
        },

        async supplyConfirmation(accept) {
            await luna.call(`${gap}supplyconfirmation`, { accept });
        },

        async supplyPasskey(passkey) {
            await luna.call(`${gap}supplypasskey`, { passkey });
        },

        async supplyPinCode(pincode) {
            await luna.call(`${gap}supplypincode`, { pincode });
        },
    };
};
