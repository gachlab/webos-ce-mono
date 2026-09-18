// The Networking settings card: captive-portal login and proxy configuration.

import { openBus } from "@webos/api/infra/luna/open-bus.ts";
import { t } from "@webos/api/i18n/translate.ts";
import { html } from "@webos/ui-kit/element.ts";
import { error as errorLine, note } from "@webos/ui-kit/kit/kit.ts";
import { startCard } from "@webos/ui-kit/start-card.ts";
import {
    createNetworkService, type NetworkData, type NetworkService, type ProxyFields,
} from "./network.service.ts";
import type { State } from "@webos/api/helpers/create-state.ts";

const proxyTypes = [
    { value: "noProxy", label: t("None") },
    { value: "manualProxy", label: t("Manual") },
    { value: "autoConfigUrl", label: t("Automatic (PAC URL)") },
    { value: "autoDetectFromNetwork", label: t("Auto-detect from network") },
];

const portal = (data: NetworkData, service: NetworkService) => html`
    ${note(data.note)}
    <div class="wos-group">
        <wos-button label=${t("Open Login Page")} kind="affirmative"
                    @press=${() => service.onOpenLogin()}></wos-button>
    </div>
    ${data.message ? errorLine(data.message) : ""}
`;

const connected = (data: NetworkData) => html`
    ${note(data.note)}
    ${data.message ? errorLine(data.message) : ""}
`;

const idle = (data: NetworkData) => html`
    ${data.note ? note(data.note) : ""}
    ${data.message ? errorLine(data.message) : ""}
`;

const proxy = (fields: ProxyFields, data: NetworkData, service: NetworkService) => html`
    <div class="wos-group">
        <div class="wos-group-title">${t("Proxy Configuration")}</div>
        <div class="wos-list">
            <wos-selector label=${t("Method")} value=${fields.type}
                .choices=${proxyTypes}
                ?open=${!!data.choosing}
                @open=${(e: CustomEvent<{ open: boolean }>) => service.onChooseType(e.detail.open)}
                @choose=${(e: CustomEvent<{ value: string }>) =>
                    service.onProxyField({ type: e.detail.value as ProxyFields["type"] })}>
            </wos-selector>
        </div>
    </div>
    ${fields.type === "manualProxy" ? html`
        <div class="wos-group">
            <div class="wos-list">
                <wos-field label=${t("Server")} value=${fields.proxyServer}
                    placeholder=${t("hostname or address")}
                    @change=${(e: CustomEvent<{ value: string }>) =>
                        service.onProxyField({ proxyServer: e.detail.value })}></wos-field>
                <wos-field label=${t("Port")} value=${fields.proxyPort}
                    placeholder=${t("optional")}
                    @change=${(e: CustomEvent<{ value: string }>) =>
                        service.onProxyField({ proxyPort: e.detail.value })}></wos-field>
                <wos-row title=${t("Secure proxy (HTTPS)")}>
                    <wos-toggle ?on=${fields.isProxySecured}
                               @toggle=${(e: CustomEvent<{ on: boolean }>) =>
                                   service.onProxyField({ isProxySecured: e.detail.on })}></wos-toggle>
                </wos-row>
            </div>
        </div>
    ` : ""}
    ${fields.type === "autoConfigUrl" ? html`
        <div class="wos-group">
            <div class="wos-list">
                <wos-field label=${t("PAC URL")} value=${fields.proxyAutoConfigUrl}
                    placeholder=${t("http://…/proxy.pac")}
                    @change=${(e: CustomEvent<{ value: string }>) =>
                        service.onProxyField({ proxyAutoConfigUrl: e.detail.value })}></wos-field>
            </div>
        </div>
    ` : ""}
    ${fields.type === "autoDetectFromNetwork" ? note(t(
        "The device will look for a proxy configuration on the network. Nothing is stored.")) : ""}
    ${fields.type === "noProxy" ? note(t("No proxy will be used for this network.")) : ""}
    ${data.message ? errorLine(data.message) : ""}
`;

const headerTitle = (screen: NetworkData["screen"]): string => {
    switch (screen) {
    case "portal": return t("Network Login");
    case "proxy": return t("Configure Proxy");
    case "connected": return t("Network Login");
    default: return t("Network");
    }
};

const footer = (data: NetworkData, service: NetworkService) => {
    if (data.screen !== "proxy")
        return "";
    return html`
        <div class="network-footer">
            <wos-button class="network-wide" label=${t("Cancel")}
                @press=${() => service.onCancelProxy()}></wos-button>
            <wos-activity-button class="network-wide" label=${t("Save")} kind="affirmative"
                ?busy=${data.busy}
                @press=${() => service.onSaveProxy()}></wos-activity-button>
        </div>`;
};

const view = (state: State<NetworkData>, service: NetworkService) => {
    const data = state.data;
    const body = data.screen === "portal" ? portal(data, service)
        : data.screen === "connected" ? connected(data)
        : data.screen === "proxy" && data.proxy ? proxy(data.proxy, data, service)
        : idle(data);
    const foot = footer(data, service);
    return html`
        <div class="wos-card ${foot ? "network-has-footer" : ""}">
            <wos-header title=${headerTitle(data.screen)} light icon="header-icon-network.png"
                       ?back=${data.screen === "proxy"}
                       @back=${() => service.onBack()}></wos-header>
            <div class="wos-body">${body}</div>
            ${foot}
        </div>
    `;
};

const luna = openBus();
const service = createNetworkService(luna);
startCard({ service, view });
