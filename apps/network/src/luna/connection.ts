// com.palm.connectionmanager for the Networking card: status with onInternet,
// and the proxy store (#23).

import type { LunaService, Payload, Subscription } from "@webos/api/infra/luna/service.ts";

const CM = "luna://com.palm.connectionmanager/";

export type ProxyConfigType =
    | "noProxy"
    | "manualProxy"
    | "autoConfigUrl"
    | "autoDetectFromNetwork";

export interface ProxyInfo {
    readonly networkTechnology: string;
    readonly proxyScope: string;
    readonly proxyConfigType: ProxyConfigType;
    readonly proxyServer?: string;
    readonly proxyPort?: number;
    readonly proxyAutoConfigUrl?: string;
    readonly isProxySecured?: boolean;
}

export interface NetworkStatus {
    readonly online: boolean;
    readonly wifiOnInternet: string;
    readonly wiredOnInternet: string;
    readonly wifiConnected: boolean;
    readonly wiredConnected: boolean;
}

interface StatusReply extends Payload {
    isInternetConnectionAvailable?: boolean;
    wifi?: { state?: string; onInternet?: string };
    wired?: { state?: string; onInternet?: string };
}

const text = (value: unknown): string => (typeof value === "string" ? value : "");
const count = (value: unknown): number | undefined =>
    typeof value === "number" && Number.isFinite(value) ? value : undefined;
const flag = (value: unknown): boolean | undefined =>
    typeof value === "boolean" ? value : undefined;

export const statusOf = (reply: StatusReply): NetworkStatus => ({
    online: reply.isInternetConnectionAvailable === true,
    wifiOnInternet: text(reply.wifi?.onInternet),
    wiredOnInternet: text(reply.wired?.onInternet),
    wifiConnected: reply.wifi?.state === "connected",
    wiredConnected: reply.wired?.state === "connected",
});

export const isCaptivePortal = (status: NetworkStatus): boolean =>
    (status.wifiConnected && status.wifiOnInternet === "captivePortal")
    || (status.wiredConnected && status.wiredOnInternet === "captivePortal");

const proxyOf = (raw: Payload): ProxyInfo | undefined => {
    const networkTechnology = text(raw.networkTechnology);
    const proxyScope = raw.proxyScope !== undefined && raw.proxyScope !== null
        ? String(raw.proxyScope) : "";
    const proxyConfigType = text(raw.proxyConfigType) as ProxyConfigType;
    if (!networkTechnology || !proxyScope || !proxyConfigType)
        return undefined;
    const info: ProxyInfo = {
        networkTechnology,
        proxyScope,
        proxyConfigType,
    };
    const server = text(raw.proxyServer);
    if (server)
        return {
            ...info,
            proxyServer: server,
            ...(count(raw.proxyPort) !== undefined ? { proxyPort: count(raw.proxyPort)! } : {}),
            ...(flag(raw.isProxySecured) !== undefined ? { isProxySecured: flag(raw.isProxySecured)! } : {}),
        };
    const pac = text(raw.proxyAutoConfigUrl);
    if (pac)
        return { ...info, proxyAutoConfigUrl: pac };
    return info;
};

export interface Connection {
    watchStatus(onStatus: (status: NetworkStatus) => void,
                onError?: (error: Error) => void): Subscription;
    getProxies(): Promise<ProxyInfo[]>;
    configureProxy(action: "add" | "rmv", proxyInfo: ProxyInfo): Promise<void>;
    checkConnectivity(): Promise<boolean>;
}

export const createConnection = (luna: LunaService): Connection => ({
    watchStatus: (onStatus, onError) =>
        luna.subscribe<StatusReply>(`${CM}getstatus`, {}, (reply) => onStatus(statusOf(reply)), onError),

    getProxies: async () => {
        const reply = await luna.call(`${CM}getNwProxiesConfig`, {});
        const list = Array.isArray(reply.proxyInfoList) ? reply.proxyInfoList as Payload[] : [];
        return list.map(proxyOf).filter((one): one is ProxyInfo => one !== undefined);
    },

    configureProxy: async (action, proxyInfo) => {
        await luna.call(`${CM}configureNwProxies`, { action, proxyInfo });
    },

    checkConnectivity: async () => {
        const reply = await luna.call(`${CM}checkNetworkConnectivity`, {});
        return reply.isInternetConnectionAvailable === true;
    },
});
