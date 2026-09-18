// The VPN settings card.
//
// Three screens from the service state: the profile list, add a profile, and
// connection details. Nothing here decides anything; it draws what the service
// says and tells it what the user did.

import { openBus } from "@webos/api/infra/luna/open-bus.ts";
import { t } from "@webos/api/i18n/translate.ts";
import { html } from "@webos/ui-kit/element.ts";
import { error as errorLine, note } from "@webos/ui-kit/kit/kit.ts";
import { startCard } from "@webos/ui-kit/start-card.ts";
import { createVpnService, stateLabel, type VpnData, type VpnService } from "./vpn.service.ts";
import type { AgentGuid, ProfileFields, VpnProfile } from "./luna/vpn.ts";
import type { State } from "@webos/api/helpers/create-state.ts";

const agentChoices = (data: VpnData) =>
    (data.agents.length > 0 ? data.agents : [
        { guid: "com.gachlab.openvpn" as AgentGuid, label: "OpenVPN", technology: "ssl" },
        { guid: "com.gachlab.wireguard" as AgentGuid, label: "WireGuard", technology: "wireguard" },
    ]).map((a) => ({ value: a.guid, label: a.label }));

const list = (data: VpnData, service: VpnService) => html`
    ${data.caption ? note(data.caption) : ""}
    <wos-group title=${t("VPN PROFILES")}>
        ${data.profiles.map((profile: VpnProfile) => html`
            <wos-row
                title=${profile.name}
                detail=${stateLabel(profile.connectState)}
                @select=${() => service.onOpenDetails(profile.name)}>
            </wos-row>
        `)}
        <wos-row title=${t("Add a Profile")} @select=${() => service.onOpenAdd()}></wos-row>
    </wos-group>
`;

const addForm = (fields: ProfileFields, data: VpnData, service: VpnService) => {
    const openvpn = fields.agentGuid === "com.gachlab.openvpn";
    return html`
        <wos-group title=${t("ADD A PROFILE")}>
            <wos-field label=${t("Profile Name")} value=${fields.name}
                @change=${(e: CustomEvent<{ value: string }>) =>
                    service.onAddField({ name: e.detail.value })}></wos-field>
            <wos-selector label=${t("Connection Type")} value=${fields.agentGuid}
                .choices=${agentChoices(data)}
                @choose=${(e: CustomEvent<{ value: string }>) =>
                    service.onAddField({ agentGuid: e.detail.value as AgentGuid })}></wos-selector>
            <wos-field label=${openvpn ? t("Server") : t("Endpoint")} value=${fields.remote}
                @change=${(e: CustomEvent<{ value: string }>) =>
                    service.onAddField({ remote: e.detail.value })}></wos-field>
            ${openvpn ? html`
                <wos-field label=${t("Username")} value=${fields.userName}
                    @change=${(e: CustomEvent<{ value: string }>) =>
                        service.onAddField({ userName: e.detail.value })}></wos-field>
                <wos-field label=${t("Password")} value=${fields.password} type="password"
                    @change=${(e: CustomEvent<{ value: string }>) =>
                        service.onAddField({ password: e.detail.value })}></wos-field>
            ` : html`
                <wos-field label=${t("Private Key")} value=${fields.privateKey}
                    @change=${(e: CustomEvent<{ value: string }>) =>
                        service.onAddField({ privateKey: e.detail.value })}></wos-field>
                <wos-field label=${t("Peer Public Key")} value=${fields.peerPublicKey}
                    @change=${(e: CustomEvent<{ value: string }>) =>
                        service.onAddField({ peerPublicKey: e.detail.value })}></wos-field>
                <wos-field label=${t("Address")} value=${fields.address}
                    @change=${(e: CustomEvent<{ value: string }>) =>
                        service.onAddField({ address: e.detail.value })}></wos-field>
            `}
        </wos-group>
        ${data.message ? errorLine(data.message) : ""}
        <wos-activity-button label=${t("Save")} kind="affirmative" ?busy=${data.busy}
            @press=${() => service.onSaveAdd()}></wos-activity-button>
    `;
};

const details = (profile: VpnProfile, data: VpnData, service: VpnService) => {
    const connected = profile.connectState === "connected"
        || profile.connectState === "connecting"
        || profile.connectState === "disconnecting";
    return html`
        <wos-group title=${t("PROFILE NAME")}>
            <wos-row title=${profile.name} detail=${stateLabel(profile.connectState)}></wos-row>
        </wos-group>
        <wos-group title=${t("CONNECTION DETAILS")}>
            <wos-row title=${stateLabel(profile.connectState) || t("DISCONNECTED")} detail=${t("STATE")}></wos-row>
            ${profile.remote ? html`<wos-row title=${profile.remote} detail=${t("SERVER")}></wos-row>` : ""}
            ${profile.userName ? html`<wos-row title=${profile.userName} detail=${t("USERNAME")}></wos-row>` : ""}
        </wos-group>
        ${data.message ? errorLine(data.message) : ""}
        <wos-activity-button
            label=${connected ? t("Disconnect") : t("Connect")}
            kind=${connected ? "negative" : "affirmative"}
            ?busy=${data.busy}
            @press=${() => service.onConnectDisconnect()}></wos-activity-button>
        <wos-button label=${t("Delete Profile")} kind="negative" ?disabled=${data.busy}
            @press=${() => service.onDelete()}></wos-button>
    `;
};

const view = (state: State<VpnData>, service: VpnService) => {
    const data = state.data;
    const body = data.screen === "add" && data.add
        ? addForm(data.add, data, service)
        : data.screen === "details" && data.details
            ? details(data.details, data, service)
            : list(data, service);
    return html`
        <div class="wos-card">
            <wos-header title=${t("VPN")} ?back=${data.screen !== "list"}
                @back=${() => service.onBack()}></wos-header>
            <div class="wos-body">${body}</div>
        </div>
    `;
};

const luna = openBus();
const service = createVpnService(luna);
startCard({ service, view });
