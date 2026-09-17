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
    // The status now, once.
    status(): Promise<ConnectionStatus>;
    // The status now and on every change, until the subscription is cancelled.
    watchStatus(onStatus: (status: ConnectionStatus) => void,
                onError?: (error: Error) => void): Subscription;
}

const GET_STATUS = "luna://com.palm.connectionmanager/getstatus";

export const createConnectionManager = (luna: LunaService): ConnectionManager => ({
    status: async () => statusOf(await luna.call<StatusReply>(GET_STATUS)),
    watchStatus: (onStatus, onError) =>
        luna.subscribe<StatusReply>(GET_STATUS, {}, (reply) => onStatus(statusOf(reply)), onError),
});
