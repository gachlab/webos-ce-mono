// The Device Info settings card.

import { openBus } from "@webos/api/infra/luna/open-bus.ts";
import { t } from "@webos/api/i18n/translate.ts";
import { html } from "@webos/ui-kit/element.ts";
import { error as errorLine, note } from "@webos/ui-kit/kit/kit.ts";
import { startCard } from "@webos/ui-kit/start-card.ts";
import {
    createDeviceInfoService, type DeviceInfoData, type DeviceInfoService,
} from "./deviceinfo.service.ts";
import type { EraseKind } from "./luna/deviceinfo.ts";
import type { State } from "@webos/api/helpers/create-state.ts";

const eraseRow = (title: string, detail: string, kind: EraseKind, service: DeviceInfoService) => html`
    <div class="deviceinfo-erase">
        <wos-button label=${title} kind=${kind === "reboot" ? "" : "negative"}
            @press=${() => service.onAskErase(kind)}></wos-button>
        <p class="deviceinfo-erase-note">${detail}</p>
    </div>`;

const main = (data: DeviceInfoData, service: DeviceInfoService) => html`
    <div class="wos-group">
        <div class="wos-group-title">${t("Name")}</div>
        <div class="wos-list">
            <wos-field label="" value=${data.deviceName} placeholder=${t("Device name")}
                @change=${(e: CustomEvent<{ value: string }>) =>
                    service.onDeviceName(e.detail.value)}></wos-field>
        </div>
    </div>
    <div class="wos-group">
        <wos-button label=${t("Save Name")} @press=${() => service.onSaveDeviceName()}></wos-button>
    </div>
    <div class="wos-group">
        <div class="wos-group-title">${t("Software")}</div>
        <div class="wos-list">
            <wos-row title=${data.device?.model || "—"} detail=${t("Model")}></wos-row>
            <wos-row title=${data.device?.version || "—"} detail=${t("Version")}></wos-row>
            <wos-row title=${data.device?.serial || "—"} detail=${t("Device id")}></wos-row>
        </div>
    </div>
    <div class="wos-group deviceinfo-actions">
        <wos-button label=${t("More Info")} @press=${() => service.onOpenMore()}></wos-button>
        <wos-button label=${t("Reset Options")} kind="negative"
            @press=${() => service.onOpenReset()}></wos-button>
    </div>
    ${data.message ? errorLine(data.message) : ""}
    ${data.busy ? html`<wos-spinner label=${t("Loading...")}></wos-spinner>` : ""}
`;

const more = (data: DeviceInfoData) => html`
    <div class="wos-group">
        <div class="wos-list">
            <wos-row title=${data.device?.model || "—"} detail=${t("Model")}></wos-row>
            <wos-row title=${data.device?.version || "—"} detail=${t("Software version")}></wos-row>
            <wos-row title=${data.device?.serial || "—"} detail=${t("Serial / NDU")}></wos-row>
        </div>
    </div>
    ${note(t("Storage and radio details follow what the host reports through the port's device profile."))}
`;

const reset = (service: DeviceInfoService) => html`
    ${eraseRow(t("Restart"), t("Shuts down and restarts the device."), "reboot", service)}
    ${eraseRow(t("Shut Down"), t("Turns the device and all radios off."), "shutdown", service)}
    ${eraseRow(t("Erase Apps & Data"),
        t("Erases applications you installed and all application settings and data."),
        "eraseApps", service)}
    ${eraseRow(t("Erase USB Drive Contents"),
        t("Erases personal files stored on the USB drive, including photos and videos you have taken."),
        "eraseUsb", service)}
    ${eraseRow(t("Erase All"),
        t("Erases applications you installed and all application settings and data. Also erases personal files stored on the USB drive, including photos and videos you have taken."),
        "eraseAll", service)}
    ${eraseRow(t("Secure Full Erase"),
        t("Secure Full Erase makes it more difficult to recover your data, but can take much longer to complete."),
        "wipe", service)}
`;

const view = (state: State<DeviceInfoData>, service: DeviceInfoService) => {
    const data = state.data;
    const title = data.screen === "main" ? t("Device information")
        : data.screen === "more" ? t("More Info")
        : t("Reset Options");
    return html`
        <div class="wos-card">
            <wos-header title=${title} light icon="header-icon-deviceinfo.png"
                       ?back=${data.screen !== "main"} @back=${() => service.onBack()}></wos-header>
            <div class="wos-body">
                ${data.screen === "main" ? main(data, service) : ""}
                ${data.screen === "more" ? more(data) : ""}
                ${data.screen === "reset" ? reset(service) : ""}
            </div>
            ${data.confirm ? html`
                <wos-dialog title=${data.confirm.title} message=${data.confirm.message}
                    .buttons=${[
                        { value: "cancel", label: t("Cancel") },
                        { value: "ok", label: data.confirm.confirm, kind: "negative" },
                    ]}
                    @dismiss=${() => service.onConfirmErase("cancel")}
                    @choose=${(e: CustomEvent<{ value: string }>) =>
                        service.onConfirmErase(e.detail.value)}></wos-dialog>` : ""}
            <wos-app-menu ?open=${data.menuOpen}
                .items=${[{ value: "help", label: t("Help") }]}
                @close=${() => service.onMenuChoice("")}
                @choose=${(e: CustomEvent<{ value: string }>) =>
                    service.onMenuChoice(e.detail.value)}></wos-app-menu>
        </div>
    `;
};

const luna = openBus();
const service = createDeviceInfoService(luna);
const { app } = startCard({ service, view });
app.on("menu", () => service.onMenu());
