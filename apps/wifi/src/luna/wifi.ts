// com.palm.wifi, typed.
//
// The vocabulary is HP's: `foundNetworks`, `networkInfo`, `connectState`,
// `signalBars`, `profileId`, and the names of the security types. It is what
// services/nm-connectionmanager answers from NetworkManager, and what enyo's
// wifi library asked for before this.
//
// Everything the card knows about the shape of a reply is in this file. What
// it means for the screen is the card's service.

import type { LunaService, Payload, Subscription } from "@webos/api/infra/luna/service.ts";

// HP's, in the order the card shows them.
export type Security = "none" | "wep" | "wpa-personal" | "enterprise";

export interface Network {
    readonly ssid: string;
    // 0 to 3, as the status bar draws it.
    readonly bars: number;
    readonly security: Security;
    // The saved profile, when there is one: a network that is remembered is
    // joined without asking for the key again.
    readonly profileId?: number;
    // Only the one being joined or joined to says how far it got.
    readonly connectState?: ConnectState;
    readonly lastError?: string;
}

// HP's connectState values, as the service sends them.
export type ConnectState =
    | "notAssociated" | "associating" | "associated" | "ipConfigured"
    | "ipFailed" | "associationFailed";

export interface WifiStatus {
    // The radio.
    readonly on: boolean;
    // What it is doing, when it is doing anything.
    readonly ssid: string;
    readonly connectState?: ConnectState;
    readonly bars: number;
    readonly ipAddress: string;
    readonly profileId?: number;
    // Why the last join failed, as the service names it
    // (IncorrectPassword, IncorrectPasskey, ...).
    readonly lastError?: string;
    // Only once joined: what the settings card shows as "BSSID ..., Channel ...".
    readonly bssid: string;
    readonly channel: number;
}

export interface SavedProfile {
    readonly profileId: number;
    readonly ssid: string;
    readonly security: Security;
    readonly staticIp: boolean;
}

// The address a joined network holds, which the settings screen shows and
// writes back.
export interface AddressInfo {
    readonly ip: string;
    readonly subnet: string;
    readonly gateway: string;
    readonly dns1: string;
    readonly dns2: string;
}

// What a join needs beyond the name, when the network is not open.
export interface JoinSecrets {
    readonly password?: string;
    // Enterprise (802.1X).
    readonly userName?: string;
    readonly phase2?: string;
}

const text = (value: unknown): string => (typeof value === "string" ? value : "");
const count = (value: unknown): number => (typeof value === "number" && Number.isFinite(value) ? value : 0);

const securityOf = (value: unknown): Security => {
    const name = text(value);
    return name === "wep" || name === "wpa-personal" || name === "enterprise" ? name : "none";
};

export const networkOf = (entry: Payload): Network => {
    const info = (entry.networkInfo ?? entry) as Payload;
    const network: Network = {
        ssid: text(info.ssid),
        bars: count(info.signalBars),
        security: securityOf(info.securityType),
        ...(count(info.profileId) > 0 ? { profileId: count(info.profileId) } : {}),
        ...(text(info.connectState) ? { connectState: text(info.connectState) as ConnectState } : {}),
        ...(text(info.lastConnectError) ? { lastError: text(info.lastConnectError) } : {}),
    };
    return network;
};

// A profile as the service writes it: the security is nested in the list
// ("security": {"securityType": ...}) and beside it in getprofile, and what
// the address screen reads is "useStaticIp".
export const profileOf = (profile: Payload): SavedProfile => {
    const security = (profile.security ?? {}) as Payload;
    return {
        profileId: count(profile.profileId),
        ssid: text(profile.ssid),
        security: securityOf(security.securityType ?? profile.securityType),
        staticIp: profile.useStaticIp === true,
    };
};

export const statusOf = (reply: Payload): WifiStatus => {
    const status = text(reply.status);
    const info = (reply.networkInfo ?? {}) as Payload;
    const ap = (reply.apInfo ?? {}) as Payload;
    return {
        on: status !== "serviceDisabled",
        ssid: text(info.ssid),
        ...(text(info.connectState) ? { connectState: text(info.connectState) as ConnectState } : {}),
        bars: count(info.signalBars),
        ipAddress: text(info.ipAddress),
        ...(count(info.profileId) > 0 ? { profileId: count(info.profileId) } : {}),
        ...(text(info.lastConnectError) ? { lastError: text(info.lastConnectError) } : {}),
        bssid: text(ap.bssid),
        channel: count(ap.channel),
    };
};

// A network is joined once it has an address; anything before that is still on
// its way, and anything else is not joined at all.
export const joined = (state: ConnectState | undefined): boolean => state === "ipConfigured";
export const joining = (state: ConnectState | undefined): boolean =>
    state === "associating" || state === "associated";

export interface Wifi {
    watchStatus(onStatus: (status: WifiStatus) => void, onError?: (error: Error) => void): Subscription;
    setRadio(on: boolean): Promise<void>;
    findNetworks(): Promise<Network[]>;
    // A network that is remembered, by its profile.
    joinSaved(profileId: number): Promise<void>;
    // One that is not: by name, with whatever its security needs.
    join(ssid: string, security: Security, secrets: JoinSecrets): Promise<void>;
    profiles(): Promise<SavedProfile[]>;
    // A saved profile and, when it is the one in use, the address it holds.
    profile(profileId: number): Promise<{ profile: SavedProfile; address?: AddressInfo }>;
    // The address screen's "save": DHCP, or the four fields, on that profile.
    setAddress(profileId: number, address?: AddressInfo): Promise<void>;
    forget(profileId: number): Promise<void>;
}

const WIFI = "luna://com.palm.wifi/";

export const createWifi = (luna: LunaService): Wifi => ({
    watchStatus: (onStatus, onError) =>
        luna.subscribe(`${WIFI}getstatus`, {}, (reply) => onStatus(statusOf(reply)), onError),

    setRadio: async (on) => {
        await luna.call(`${WIFI}setstate`, { state: on ? "enabled" : "disabled" });
    },

    findNetworks: async () => {
        const reply = await luna.call(`${WIFI}findnetworks`, {});
        const found = Array.isArray(reply.foundNetworks) ? reply.foundNetworks : [];
        return found.map((entry) => networkOf(entry as Payload));
    },

    joinSaved: async (profileId) => {
        await luna.call(`${WIFI}connect`, { profileId }, { timeoutMs: 60_000 });
    },

    join: async (ssid, security, secrets) => {
        // The shape enyo's library used, which is the one the service reads:
        // the security under `security.simpleSecurity`, and enterprise's user
        // name beside it.
        const payload: Payload = { ssid };
        if (security === "wpa-personal" || security === "wep") {
            payload.security = { securityType: security, simpleSecurity: { passKey: secrets.password ?? "" } };
        } else if (security === "enterprise") {
            payload.security = {
                securityType: "enterprise",
                enterpriseSecurity: {
                    userName: secrets.userName ?? "",
                    password: secrets.password ?? "",
                    ...(secrets.phase2 ? { phase2: secrets.phase2 } : {}),
                },
            };
        }
        await luna.call(`${WIFI}connect`, payload, { timeoutMs: 60_000 });
    },

    profiles: async () => {
        const reply = await luna.call(`${WIFI}getprofilelist`, {});
        const list = Array.isArray(reply.profileList) ? reply.profileList : [];
        return list.map((entry) => profileOf(((entry as Payload).wifiProfile ?? entry) as Payload));
    },

    profile: async (profileId) => {
        const reply = await luna.call(`${WIFI}getprofile`, { profileId });
        const profile = profileOf((reply.wifiProfile ?? {}) as Payload);
        const ip = reply.ipInfo as Payload | undefined;
        return ip
            ? {
                profile,
                address: {
                    ip: text(ip.ip), subnet: text(ip.subnet), gateway: text(ip.gateway),
                    dns1: text(ip.dns1), dns2: text(ip.dns2),
                },
            }
            : { profile };
    },

    setAddress: async (profileId, address) => {
        // The service replaces the profile's ipv4 with what arrives here and
        // brings the profile up again.
        await luna.call(`${WIFI}getprofile`, { profileId });
        await luna.call(`${WIFI}connect`, {
            profileId,
            useStaticIp: address !== undefined,
            ...(address
                ? { ipAddress: address.ip, subnet: address.subnet, gateway: address.gateway,
                    dns1: address.dns1, dns2: address.dns2 }
                : {}),
        }, { timeoutMs: 60_000 });
    },

    forget: async (profileId) => {
        await luna.call(`${WIFI}deleteprofile`, { profileId });
    },
});
