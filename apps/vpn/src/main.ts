// The VPN settings card.
//
// Three screens from the service state: the profile list, add a profile, and
// connection details. Layout and wording follow HP's card on the TouchPad CE
// image (spec, not code): light header with the VPN icon, "Choose a Profile",
// checkmark when connected, (i) for details, tap the name to connect, swipe to
// delete, and "Add profile..." with the plus.

import { openBus } from "@webos/api/infra/luna/open-bus.ts";
import { t } from "@webos/api/i18n/translate.ts";
import { html } from "@webos/ui-kit/element.ts";
import { error as errorLine, note } from "@webos/ui-kit/kit/kit.ts";
import { startCard } from "@webos/ui-kit/start-card.ts";
import {
    createVpnService, isActiveState, isBusyState, progressLabel, stateLabel,
    type VpnData, type VpnService,
} from "./vpn.service.ts";
import type { AgentGuid, ProfileFields, VpnProfile } from "./luna/vpn.ts";
import type { State } from "@webos/api/helpers/create-state.ts";

const agentChoices = (data: VpnData) =>
    (data.agents.length > 0 ? data.agents : [
        { guid: "com.gachlab.openvpn" as AgentGuid, label: "OpenVPN", technology: "ssl" },
        { guid: "com.gachlab.wireguard" as AgentGuid, label: "WireGuard", technology: "wireguard" },
    ]).map((a) => ({ value: a.guid, label: a.label }));

// Onyx checkmark.png (Apache, from enyo in this tree).
const tick = () => html`
    <img class="vpn-joined" src="images/checkmark.png" alt=${t("Connected")}>`;

// list-icon-add-item.png: a soft grey plus at the left of "Add profile...".
const plus = () => html`
    <svg class="vpn-plus" slot="lead" viewBox="0 0 18 18" aria-hidden="true">
        <path d="M7 0h4v7h7v4h-7v7h-4v-7H0V7h7z"></path>
    </svg>`;

const marks = (profile: VpnProfile, service: VpnService) => {
    const busy = isBusyState(profile.connectState);
    const connected = profile.connectState === "connected";
    return html`
        <span class="vpn-marks">
            ${busy ? html`<span class="vpn-spinner" aria-hidden="true"></span>` : ""}
            ${connected ? tick() : ""}
            <wos-info @press=${() => service.onOpenDetails(profile.name)}></wos-info>
        </span>`;
};

const list = (data: VpnData, service: VpnService) => html`
    ${data.caption ? note(data.caption) : ""}
    <div class="wos-group">
        <div class="wos-group-title">${t("Choose a Profile")}</div>
        <div class="wos-list">
            ${data.profiles.map((profile) => {
                const busy = isBusyState(profile.connectState);
                const active = isActiveState(profile.connectState);
                const detail = progressLabel(profile.connectState);
                const open = data.swipeOpen === profile.name;
                // HP disables swipe while the tunnel is moving.
                if (busy) {
                    return html`
                        <wos-row title=${profile.name} detail=${detail}
                                ?strong=${true}
                                @select=${() => service.onToggleConnect(profile.name)}>
                            ${marks(profile, service)}
                        </wos-row>`;
                }
                return html`
                    <wos-swipe-row
                        title=${profile.name}
                        detail=${detail}
                        confirm=${t("Delete")}
                        ?strong=${active}
                        ?open=${open}
                        @select=${() => service.onToggleConnect(profile.name)}
                        @open=${(e: CustomEvent<{ open: boolean }>) =>
                            service.onSwipe(profile.name, e.detail.open)}
                        @remove=${() => service.onDeleteProfile(profile.name)}>
                        ${marks(profile, service)}
                    </wos-swipe-row>`;
            })}
            <wos-row class="vpn-add" title=${t("Add profile...")}
                    @select=${() => service.onOpenAdd()}>${plus()}</wos-row>
        </div>
    </div>
    ${data.message ? errorLine(data.message) : ""}
`;

const addForm = (fields: ProfileFields, data: VpnData, service: VpnService) => {
    const openvpn = fields.agentGuid === "com.gachlab.openvpn";
    return html`
        <div class="wos-group">
            <div class="wos-group-title">${t("Add a Profile")}</div>
            <div class="wos-list">
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
            </div>
        </div>
        ${data.message ? errorLine(data.message) : ""}
        <div class="wos-group">
            <wos-activity-button label=${t("Connect")} kind="affirmative" ?busy=${data.busy}
                @press=${() => service.onSaveAdd()}></wos-activity-button>
        </div>
    `;
};

const details = (profile: VpnProfile, data: VpnData, service: VpnService) => {
    const active = isActiveState(profile.connectState);
    return html`
        <div class="wos-group">
            <div class="wos-group-title">${t("Profile Name")}</div>
            <div class="wos-list">
                <wos-row title=${profile.name}
                        detail=${progressLabel(profile.connectState)}
                        ?strong=${isActiveState(profile.connectState)}></wos-row>
            </div>
        </div>
        <div class="wos-group">
            <div class="wos-group-title">${t("Connection Details")}</div>
            <div class="wos-list">
                <wos-row title=${stateLabel(profile.connectState)} detail=${t("STATE")}></wos-row>
                ${profile.remote ? html`<wos-row title=${profile.remote} detail=${t("SERVER")}></wos-row>` : ""}
                ${profile.userName ? html`<wos-row title=${profile.userName} detail=${t("USERNAME")}></wos-row>` : ""}
            </div>
        </div>
        ${data.message ? errorLine(data.message) : ""}
        <div class="wos-group">
            <wos-activity-button
                label=${active ? t("Disconnect") : t("Connect")}
                kind=${active ? "negative" : "affirmative"}
                ?busy=${data.busy || isBusyState(profile.connectState)}
                @press=${() => service.onConnectDisconnect()}></wos-activity-button>
        </div>
        <div class="wos-group">
            <wos-button label=${t("Delete Profile")} kind="negative"
                ?disabled=${data.busy || isBusyState(profile.connectState)}
                @press=${() => service.onDelete()}></wos-button>
        </div>
    `;
};

const view = (state: State<VpnData>, service: VpnService) => {
    const data = state.data;
    const onList = data.screen === "list";
    const body = data.screen === "add" && data.add
        ? addForm(data.add, data, service)
        : data.screen === "details" && data.details
            ? details(data.details, data, service)
            : list(data, service);
    return html`
        <div class="wos-card">
            <wos-header title=${t("VPN")} light icon="header-icon-vpn.png"
                       ?back=${!onList} @back=${() => service.onBack()}></wos-header>
            <div class="wos-body">${body}</div>
        </div>
    `;
};

const luna = openBus();
const service = createVpnService(luna);
startCard({ service, view });
