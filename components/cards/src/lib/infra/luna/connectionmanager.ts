// com.palm.connectionmanager, typed.
//
// The layer between the bus and a card's state machine: what a service is
// called, what it answers, and the shapes its replies come in. A card's service
// asks this, never the bus directly, so a uri or a field name appears once.
//
// The rest of the port is the transport (service.ts); this is one service on
// it, the way node-services' kit/db8.ts sits on kit/luna.ts.

import type { LunaService, Payload, Subscription } from "./service.ts";

export interface ConnectionStatus {
    // What the card shows: whether anything can be reached at all.
    readonly online: boolean;
    // "wifi", "wan", "wired" or "" -- what is carrying it.
    readonly through: string;
    readonly ssid: string;
    readonly ipAddress: string;
}

interface StatusReply extends Payload {
    isInternetConnectionAvailable?: boolean;
    wifi?: { state?: string; ssid?: string; ipAddress?: string; onInternet?: string };
    wired?: { state?: string; ipAddress?: string };
    wan?: { state?: string; ipAddress?: string };
}

const connected = (part: { state?: string } | undefined): boolean => part?.state === "connected";

export const statusOf = (reply: StatusReply): ConnectionStatus => {
    const through = connected(reply.wifi) ? "wifi"
        : connected(reply.wired) ? "wired"
        : connected(reply.wan) ? "wan"
        : "";
    const part = through === "wifi" ? reply.wifi : through === "wired" ? reply.wired : reply.wan;
    return {
        online: reply.isInternetConnectionAvailable === true,
        through,
        ssid: reply.wifi?.ssid ?? "",
        ipAddress: (part as { ipAddress?: string } | undefined)?.ipAddress ?? "",
    };
};

export interface ConnectionManager {
    // What the radio does while the machine sleeps: "enable" keeps Wi-Fi on,
    // "disable" turns it off. HP's phone setting, kept for a laptop.
    wakeOnWifi(): Promise<string>;
    // Answers the mode now in force, which is what the card shows.
    setWakeOnWifi(mode: string): Promise<string>;
    // The status now, once.
    status(): Promise<ConnectionStatus>;
    // The status now and on every change, until the subscription is cancelled.
    watchStatus(onStatus: (status: ConnectionStatus) => void,
                onError?: (error: Error) => void): Subscription;
}

const GET_STATUS = "luna://com.palm.connectionmanager/getstatus";
const GET_WAKE = "luna://com.palm.connectionmanager/getWakeOnWiFiMode";
const SET_WAKE = "luna://com.palm.connectionmanager/setWakeOnWiFiMode";

const modeOf = (reply: Payload): string => (reply.mode === "enable" || reply.mode === "disable" ? reply.mode : "");

export const createConnectionManager = (luna: LunaService): ConnectionManager => ({
    wakeOnWifi: async () => modeOf(await luna.call(GET_WAKE, {})),
    setWakeOnWifi: async (mode) => modeOf(await luna.call(SET_WAKE, { mode })),
    status: async () => statusOf(await luna.call<StatusReply>(GET_STATUS)),
    watchStatus: (onStatus, onError) =>
        luna.subscribe<StatusReply>(GET_STATUS, {}, (reply) => onStatus(statusOf(reply)), onError),
});
