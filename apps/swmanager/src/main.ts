// The Software Manager settings card.

import { openBus } from "@webos/api/infra/luna/open-bus.ts";
import { t } from "@webos/api/i18n/translate.ts";
import { html } from "@webos/ui-kit/element.ts";
import { error as errorLine, note } from "@webos/ui-kit/kit/kit.ts";
import { startCard } from "@webos/ui-kit/start-card.ts";
import {
    createSwManagerService, type SwManagerData, type SwManagerService,
} from "./swmanager.service.ts";
import type { InstalledApp } from "./luna/swmanager.ts";
import type { State } from "@webos/api/helpers/create-state.ts";

const list = (data: SwManagerData, service: SwManagerService) => html`
    ${data.apps.length === 0 && !data.busy
        ? note(t("No applications found."))
        : ""}
    <div class="wos-group">
        <div class="wos-group-title">${t("Installed applications")}</div>
        <div class="wos-list">
            ${data.apps.map((app: InstalledApp) => html`
                <wos-row title=${app.title}
                    detail=${app.version}
                    @select=${() => service.onOpenDetails(app.id)}></wos-row>
            `)}
        </div>
    </div>
    ${data.message ? errorLine(data.message) : ""}
    ${data.busy ? html`<wos-spinner label=${t("Loading...")}></wos-spinner>` : ""}
`;

const details = (app: InstalledApp, data: SwManagerData, service: SwManagerService) => html`
    <div class="wos-group">
        <div class="wos-group-title">${t("Application")}</div>
        <div class="wos-list">
            <wos-row title=${app.title} detail=${t("Name")}></wos-row>
            <wos-row title=${app.id} detail=${t("Id")}></wos-row>
            ${app.version
                ? html`<wos-row title=${app.version} detail=${t("Version")}></wos-row>`
                : ""}
            ${app.vendor
                ? html`<wos-row title=${app.vendor} detail=${t("Vendor")}></wos-row>`
                : ""}
            <wos-row title=${app.removable ? t("Yes") : t("No")}
                detail=${t("Removable")}></wos-row>
        </div>
    </div>
    ${data.message ? errorLine(data.message) : ""}
    <div class="wos-group">
        <wos-button label=${t("Delete")} kind="negative"
            ?disabled=${data.busy || !app.removable}
            @press=${() => service.onAskDelete()}></wos-button>
    </div>
    ${!app.removable
        ? note(t("This application cannot be deleted from your device."))
        : ""}
`;

const view = (state: State<SwManagerData>, service: SwManagerService) => {
    const data = state.data;
    const onList = data.screen === "list";
    const body = data.screen === "details" && data.details
        ? details(data.details, data, service)
        : list(data, service);
    return html`
        <div class="wos-card">
            <wos-header title=${onList ? t("Software Manager") : (data.details?.title ?? t("Details"))}
                       light icon="header-icon-swmanager.png"
                       ?back=${!onList} @back=${() => service.onBack()}></wos-header>
            <div class="wos-body">${body}</div>
            ${data.confirmDelete && data.details ? html`
                <wos-dialog title=${`${t("Delete")} ${data.details.title}`}
                    message=${t("Are you sure you want to delete this application?")}
                    .buttons=${[
                        { value: "ok", label: t("Delete"), kind: "negative" },
                        { value: "cancel", label: t("Cancel") },
                    ]}
                    @choose=${(e: CustomEvent<{ value: string }>) =>
                        service.onConfirmDelete(e.detail.value === "ok" ? "ok" : "cancel")}
                    @dismiss=${() => service.onConfirmDelete("cancel")}></wos-dialog>
            ` : ""}
            <wos-app-menu ?open=${data.menuOpen}
                .items=${[{ value: "help", label: t("Help") }]}
                @close=${() => service.onMenuChoice("")}
                @choose=${(e: CustomEvent<{ value: string }>) =>
                    service.onMenuChoice(e.detail.value)}></wos-app-menu>
        </div>
    `;
};

const luna = openBus();
const service = createSwManagerService(luna);
const { app } = startCard({ service, view });
app.on("menu", () => service.onMenu());
