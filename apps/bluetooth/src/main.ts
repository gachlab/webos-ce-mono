// The Bluetooth settings card.
//
// Screens follow HP's bluetoothtab on the TouchPad CE image (spec, not code):
// the radio switch over the device list, a pairing screen for the PIN / passkey
// / numeric-comparison prompts BlueZ raises, and connection details.

import { openBus } from "@webos/api/infra/luna/open-bus.ts";
import { t } from "@webos/api/i18n/translate.ts";
import { html } from "@webos/ui-kit/element.ts";
import { error as errorLine, note } from "@webos/ui-kit/kit/kit.ts";
import { startCard } from "@webos/ui-kit/start-card.ts";
import {
    createBtService, radioOn, radioBusy, statusLabel,
    type BtData, type BtService,
} from "./bluetooth.service.ts";
import type { Device, PairingPrompt } from "./luna/bluetooth.ts";
import type { State } from "@webos/api/helpers/create-state.ts";

// Onyx checkmark.png (Apache, from enyo in this tree), as the VPN card uses.
const tick = () => html`
    <img class="bt-joined" src="images/checkmark.png" alt=${t("Connected")}>`;

// The battery and the connection line HP shows under a device's name.
const detailLine = (device: Device): string => {
    const moving = statusLabel(device.status);
    if (moving)
        return moving;
    if (device.status === "connected")
        return device.battery !== undefined ? `${t("Connected")} · ${device.battery}%` : t("Connected");
    return device.paired ? t("Paired") : "";
};

const marks = (device: Device, service: BtService) => {
    const busy = device.status === "connecting" || device.status === "disconnecting";
    const connected = device.status === "connected";
    return html`
        <span class="bt-marks">
            ${busy ? html`<span class="bt-spinner" aria-hidden="true"></span>` : ""}
            ${connected ? tick() : ""}
            <wos-info @press=${() => service.onOpenDetails(device.address)}></wos-info>
        </span>`;
};

const deviceRow = (device: Device, data: BtData, service: BtService) => {
    const busy = device.status === "connecting" || device.status === "disconnecting";
    const active = device.status === "connected" || busy;
    const open = data.swipeOpen === device.address;
    if (busy) {
        return html`
            <wos-row title=${device.name} detail=${detailLine(device)} ?strong=${true}
                    @select=${() => service.onTapDevice(device.address)}>
                ${marks(device, service)}
            </wos-row>`;
    }
    return html`
        <wos-swipe-row
            title=${device.name}
            detail=${detailLine(device)}
            confirm=${t("Forget")}
            ?strong=${active}
            ?open=${open}
            @select=${() => service.onTapDevice(device.address)}
            @open=${(e: CustomEvent<{ open: boolean }>) => service.onSwipe(device.address, e.detail.open)}
            @remove=${() => service.onForget(device.address)}>
            ${marks(device, service)}
        </wos-swipe-row>`;
};

const list = (data: BtData, service: BtService) => {
    const on = radioOn(data.radio);
    return html`
        <div class="wos-group">
            <div class="wos-list">
                <wos-row title=${t("Bluetooth")}>
                    <wos-toggle slot="end" ?on=${on} ?disabled=${radioBusy(data.radio)}
                        label-on=${t("On")} label-off=${t("Off")}
                        @toggle=${() => service.onToggleRadio()}></wos-toggle>
                </wos-row>
            </div>
        </div>
        ${on ? html`
            ${data.caption ? note(data.caption) : ""}
            <div class="wos-group">
                <div class="wos-group-title">${t("Devices")}</div>
                <div class="wos-list">
                    ${data.devices.map((device) => deviceRow(device, data, service))}
                    <wos-row class="bt-scan" title=${data.discovering ? t("Searching...") : t("Add device...")}
                            @select=${() => service.onToggleDiscovery()}>
                        ${data.discovering ? html`<span class="bt-spinner" slot="lead" aria-hidden="true"></span>` : ""}
                    </wos-row>
                </div>
            </div>
        ` : note(t("Turn on Bluetooth to connect devices."))}
        ${data.message ? errorLine(data.message) : ""}
    `;
};

// The pairing screen renders one of BlueZ's prompts. Numeric comparison and
// incoming authorization are yes/no; passkey and PIN take a typed value;
// display-only prompts show a code with a single OK.
const pairing = (prompt: PairingPrompt, data: BtData, service: BtService) => {
    const name = prompt.name || prompt.address;
    const confirmKind = prompt.kind === "requestconfirmation" || prompt.kind === "requestauthorization";
    const entryKind = prompt.kind === "requestpasskey" || prompt.kind === "requestpincode";
    const displayKind = prompt.kind === "displaypasskey" || prompt.kind === "displaypincode";
    const code = prompt.passkey || prompt.pincode || "";
    return html`
        <div class="wos-group">
            <div class="wos-group-title">${t("Pairing with")} ${name}</div>
            <div class="wos-list">
                ${confirmKind ? html`
                    <wos-row title=${code} detail=${t("CONFIRM THIS CODE MATCHES")}></wos-row>
                ` : ""}
                ${displayKind ? html`
                    <wos-row title=${code} detail=${t("ENTER THIS ON THE DEVICE")}></wos-row>
                ` : ""}
                ${entryKind ? html`
                    <wos-field label=${prompt.kind === "requestpincode" ? t("PIN") : t("Passkey")}
                        value=${data.entry ?? ""}
                        type=${prompt.kind === "requestpincode" ? "text" : "number"}
                        @change=${(e: CustomEvent<{ value: string }>) => service.onPromptEntry(e.detail.value)}
                        @done=${() => service.onPromptAccept()}></wos-field>
                ` : ""}
            </div>
        </div>
        ${data.message ? errorLine(data.message) : ""}
        <div class="wos-group bt-prompt-buttons">
            ${displayKind ? html`
                <wos-button label=${t("OK")} kind="affirmative"
                    @press=${() => service.onPromptAccept()}></wos-button>
            ` : html`
                <wos-button label=${confirmKind ? t("No") : t("Cancel")} kind="negative"
                    @press=${() => service.onPromptReject()}></wos-button>
                <wos-activity-button label=${confirmKind ? t("Yes") : t("Pair")} kind="affirmative"
                    ?busy=${data.busy}
                    @press=${() => service.onPromptAccept()}></wos-activity-button>
            `}
        </div>
    `;
};

const details = (device: Device, data: BtData, service: BtService) => {
    const connected = device.status === "connected";
    const busy = device.status === "connecting" || device.status === "disconnecting";
    return html`
        <div class="wos-group">
            <div class="wos-group-title">${t("Device")}</div>
            <div class="wos-list">
                <wos-row title=${device.name} detail=${detailLine(device)} ?strong=${connected}></wos-row>
                ${device.battery !== undefined
                    ? html`<wos-row title=${`${device.battery}%`} detail=${t("BATTERY")}></wos-row>` : ""}
                <wos-row title=${device.address} detail=${t("ADDRESS")}></wos-row>
            </div>
        </div>
        ${data.message ? errorLine(data.message) : ""}
        <div class="wos-group">
            <wos-activity-button
                label=${connected ? t("Disconnect") : t("Connect")}
                kind=${connected ? "negative" : "affirmative"}
                ?busy=${data.busy || busy}
                @press=${() => service.onConnectDisconnect()}></wos-activity-button>
        </div>
        <div class="wos-group">
            <wos-button label=${t("Forget Device")} kind="negative"
                ?disabled=${data.busy || busy}
                @press=${() => service.onForgetDetails()}></wos-button>
        </div>
    `;
};

const headerTitle = (data: BtData): string => {
    switch (data.screen) {
    case "pairing": return t("Pairing");
    case "details": return t("Device Info");
    default: return t("Bluetooth");
    }
};

const view = (state: State<BtData>, service: BtService) => {
    const data = state.data;
    // The header back shows on details only: the pairing screen has its own
    // Cancel/No/OK buttons (like VPN's add/configure footer), so a second back
    // in the header would be a redundant -- and conflicting -- way to dismiss it.
    const showBack = data.screen === "details";
    const body = data.screen === "pairing" && data.prompt
        ? pairing(data.prompt, data, service)
        : data.screen === "details" && data.details
            ? details(data.details, data, service)
            : list(data, service);
    return html`
        <div class="wos-card">
            <wos-header title=${headerTitle(data)} light icon="header-icon-bluetooth.png"
                       ?back=${showBack}
                       @back=${() => service.onBack()}></wos-header>
            <div class="wos-body">${body}</div>
        </div>
    `;
};

const luna = openBus();
const service = createBtService(luna);
startCard({ service, view });
