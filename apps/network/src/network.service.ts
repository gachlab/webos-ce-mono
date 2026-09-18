// The Networking card: captive-portal login and per-network proxy settings.
//
// Screens and wording follow HP's Networking card on the TouchPad CE image
// (spec, not code). The service is com.palm.connectionmanager.

import { createState, type State, type StateHolder, type Unsubscribe } from "@webos/api/helpers/create-state.ts";
import {
    createConnection, isCaptivePortal, type NetworkStatus, type ProxyConfigType, type ProxyInfo,
} from "./luna/connection.ts";
import { LunaCallError, errorTextOf, type LunaService, type Subscription } from "@webos/api/infra/luna/service.ts";
import type { LaunchParams } from "@webos/api/infra/app/service.ts";

export type NetworkScreen = "idle" | "portal" | "proxy" | "connected";

export type ProxyFormType = "noProxy" | "manualProxy" | "autoConfigUrl" | "autoDetectFromNetwork";

export interface ProxyFields {
    readonly networkTechnology: string;
    readonly proxyScope: string;
    readonly type: ProxyFormType;
    readonly proxyServer: string;
    readonly proxyPort: string;
    readonly proxyAutoConfigUrl: string;
    readonly isProxySecured: boolean;
}

export interface NetworkData {
    readonly screen: NetworkScreen;
    readonly note: string;
    readonly busy: boolean;
    readonly message: string;
    readonly choosing?: boolean;
    readonly proxy?: ProxyFields;
    readonly status?: NetworkStatus;
}

export interface NetworkService {
    getState(): State<NetworkData>;
    onStateChange(listener: (state: State<NetworkData>) => void): Unsubscribe;
    onShown(params?: LaunchParams): void;
    onHidden(): void;
    onBack(): boolean;
    dispose(): void;

    onOpenLogin(): void;
    onProxyField(change: Partial<ProxyFields>): void;
    onChooseType(open: boolean): void;
    onSaveProxy(): void;
    onCancelProxy(): void;
}

// Captive probe URL opened in the browser. Google's generate_204 is what
// Android and many portals expect; Firefox's success.txt is an alternative.
export const CAPTIVE_PROBE_URL = "http://connectivitycheck.gstatic.com/generate_204";

const emptyProxy = (technology: string, scope: string): ProxyFields => ({
    networkTechnology: technology,
    proxyScope: scope,
    type: "noProxy",
    proxyServer: "",
    proxyPort: "",
    proxyAutoConfigUrl: "",
    isProxySecured: false,
});

const fieldsFrom = (info: ProxyInfo | undefined, technology: string, scope: string): ProxyFields => {
    if (!info)
        return emptyProxy(technology, scope);
    return {
        networkTechnology: info.networkTechnology,
        proxyScope: info.proxyScope,
        type: info.proxyConfigType,
        proxyServer: info.proxyServer ?? "",
        proxyPort: info.proxyPort !== undefined ? String(info.proxyPort) : "",
        proxyAutoConfigUrl: info.proxyAutoConfigUrl ?? "",
        isProxySecured: info.isProxySecured === true,
    };
};

const proxyLaunch = (params?: LaunchParams): { technology: string; scope: string } | undefined => {
    if (!params)
        return undefined;
    const mode = typeof params.mode === "string" ? params.mode : "";
    const technology = typeof params.networkTechnology === "string" ? params.networkTechnology : "";
    const scope = params.proxyScope !== undefined && params.proxyScope !== null
        ? String(params.proxyScope) : "";
    if (mode === "proxy" || (technology && scope)) {
        if (!technology || !scope)
            return undefined;
        return { technology, scope };
    }
    return undefined;
};

export const createNetworkService = (luna: LunaService): NetworkService => {
    const connection = createConnection(luna);
    const state: StateHolder<NetworkData> = createState<NetworkData>({
        name: "network:idle",
        data: { screen: "idle", note: "", busy: false, message: "" },
    });
    let gone = false;
    let subscription: Subscription | undefined;
    let wantProxy: { technology: string; scope: string } | undefined;

    const fail = (error: unknown) => {
        if (gone)
            return;
        const message = error instanceof LunaCallError ? errorTextOf(error.reply)
            : error instanceof Error ? error.message : String(error);
        state.patch({ busy: false, message });
    };

    const showPortal = (status: NetworkStatus) => {
        state.set({
            name: "network:portal",
            data: {
                screen: "portal",
                note: "This network needs a sign-in page before the internet is available.",
                busy: false,
                message: "",
                status,
            },
        });
    };

    const showConnected = (status: NetworkStatus) => {
        state.set({
            name: "network:connected",
            data: {
                screen: "connected",
                note: "You are connected to the internet.",
                busy: false,
                message: "",
                status,
            },
        });
    };

    const showIdle = (status?: NetworkStatus) => {
        state.set({
            name: "network:idle",
            data: {
                screen: "idle",
                note: "No network login is required right now.",
                busy: false,
                message: "",
                ...(status ? { status } : {}),
            },
        });
    };

    const openProxy = (technology: string, scope: string) => {
        state.patch({ busy: true, message: "" });
        void connection.getProxies().then((list) => {
            if (gone)
                return;
            const match = list.find((one) =>
                one.networkTechnology === technology && one.proxyScope === scope);
            state.set({
                name: "network:proxy",
                data: {
                    screen: "proxy",
                    note: "",
                    busy: false,
                    message: "",
                    proxy: fieldsFrom(match, technology, scope),
                },
            });
        }).catch(fail);
    };

    const applyStatus = (status: NetworkStatus) => {
        if (gone)
            return;
        const current = state.get().data.screen;
        // Proxy mode is driven by launch params; do not steal it for a portal.
        if (wantProxy || current === "proxy") {
            state.patch({ status });
            return;
        }
        if (isCaptivePortal(status)) {
            showPortal(status);
            return;
        }
        if (current === "portal" && status.online) {
            showConnected(status);
            return;
        }
        if (current === "portal" && !isCaptivePortal(status) && !status.online) {
            showIdle(status);
            return;
        }
        if (current === "idle" || current === "connected")
            state.patch({ status });
    };

    return {
        getState: () => state.get(),
        onStateChange: (listener) => state.subscribe(listener),

        onShown(params) {
            gone = false;
            wantProxy = proxyLaunch(params);
            if (wantProxy)
                openProxy(wantProxy.technology, wantProxy.scope);
            if (subscription)
                return;
            subscription = connection.watchStatus(applyStatus, (error) => fail(error));
        },

        onHidden() {
            subscription?.cancel();
            subscription = undefined;
        },

        onBack() {
            if (state.get().data.screen === "proxy") {
                wantProxy = undefined;
                const status = state.get().data.status;
                if (status && isCaptivePortal(status))
                    showPortal(status);
                else
                    showIdle(status);
                return true;
            }
            return false;
        },

        dispose() {
            gone = true;
            subscription?.cancel();
            subscription = undefined;
            state.clear();
        },

        onOpenLogin() {
            void luna.call("luna://com.palm.applicationManager/open", { target: CAPTIVE_PROBE_URL })
                .catch(fail);
        },

        onProxyField(change) {
            const proxy = state.get().data.proxy;
            if (!proxy)
                return;
            state.patch({ proxy: { ...proxy, ...change }, message: "", choosing: false });
        },

        onChooseType(open) {
            state.patch({ choosing: open });
        },

        onSaveProxy() {
            const proxy = state.get().data.proxy;
            if (!proxy || state.get().data.busy)
                return;
            const type = proxy.type as ProxyConfigType;
            if (type === "manualProxy" && !proxy.proxyServer.trim()) {
                state.patch({ message: "Enter a proxy server." });
                return;
            }
            if (type === "autoConfigUrl" && !proxy.proxyAutoConfigUrl.trim()) {
                state.patch({ message: "Enter a proxy auto-config URL." });
                return;
            }
            state.patch({ busy: true, message: "" });
            const run = async () => {
                if (type === "noProxy") {
                    await connection.configureProxy("rmv", {
                        networkTechnology: proxy.networkTechnology,
                        proxyScope: proxy.proxyScope,
                        proxyConfigType: "noProxy",
                    });
                } else {
                    const port = proxy.proxyPort.trim() === "" ? undefined : Number(proxy.proxyPort);
                    if (proxy.proxyPort.trim() !== "" && !Number.isFinite(port))
                        throw new Error("Proxy port must be a number.");
                    await connection.configureProxy("add", {
                        networkTechnology: proxy.networkTechnology,
                        proxyScope: proxy.proxyScope,
                        proxyConfigType: type,
                        ...(type === "manualProxy" ? {
                            proxyServer: proxy.proxyServer.trim(),
                            ...(port !== undefined ? { proxyPort: port } : {}),
                            isProxySecured: proxy.isProxySecured,
                        } : {}),
                        ...(type === "autoConfigUrl"
                            ? { proxyAutoConfigUrl: proxy.proxyAutoConfigUrl.trim() }
                            : {}),
                    });
                }
                if (!gone) {
                    wantProxy = undefined;
                    state.patch({ busy: false, message: "", note: "Proxy settings saved." });
                    showIdle(state.get().data.status);
                }
            };
            void run().catch(fail);
        },

        onCancelProxy() {
            wantProxy = undefined;
            showIdle(state.get().data.status);
        },
    };
};
