// The Date & Time settings card.

import { openBus } from "@webos/api/infra/luna/open-bus.ts";
import { t } from "@webos/api/i18n/translate.ts";
import { html } from "@webos/ui-kit/element.ts";
import { error as errorLine } from "@webos/ui-kit/kit/kit.ts";
import { startCard } from "@webos/ui-kit/start-card.ts";
import {
    createDateTimeService, filterZones, FORMAT_CHOICES,
    type DateTimeData, type DateTimeService,
} from "./dateandtime.service.ts";
import { zoneLabel } from "./luna/dateandtime.ts";
import type { State } from "@webos/api/helpers/create-state.ts";

const main = (data: DateTimeData, service: DateTimeService) => html`
    <div class="wos-group">
        <div class="wos-list">
            <wos-selector label=${t("Time Format")} value=${data.timeFormat}
                .choices=${FORMAT_CHOICES}
                ?open=${!!data.choosingFormat}
                @open=${(e: CustomEvent<{ open: boolean }>) => service.onChooseFormat(e.detail.open)}
                @choose=${(e: CustomEvent<{ value: string }>) =>
                    service.onTimeFormat(e.detail.value as "HH12" | "HH24")}></wos-selector>
            <wos-row title=${t("Network Time")}>
                <wos-toggle ?on=${data.useNetworkTime}
                    @toggle=${(e: CustomEvent<{ on: boolean }>) =>
                        service.onNetworkTime(e.detail.on)}></wos-toggle>
            </wos-row>
        </div>
    </div>
    <p class="dateandtime-now">${data.nowLabel || "—"}</p>
    ${!data.useNetworkTime ? html`
        <div class="wos-group">
            <div class="wos-list">
                <wos-field label=${t("Date")} value=${data.dateValue} type="date"
                    @change=${(e: CustomEvent<{ value: string }>) =>
                        service.onDateValue(e.detail.value)}></wos-field>
                <wos-field label=${t("Time")} value=${data.timeValue} type="time"
                    @change=${(e: CustomEvent<{ value: string }>) =>
                        service.onTimeValue(e.detail.value)}></wos-field>
            </div>
        </div>
        <div class="wos-group">
            <wos-activity-button label=${t("Set Date & Time")} kind="affirmative"
                ?busy=${data.busy} @press=${() => service.onApplyManualTime()}></wos-activity-button>
        </div>
    ` : ""}
    <div class="wos-group">
        <div class="wos-group-title">${t("Time Zone")}</div>
        <div class="wos-list">
            <wos-row title=${zoneLabel(data.timeZone) || t("Choose time zone")}
                @select=${() => service.onOpenTimezone()}></wos-row>
            <wos-row title=${t("Network time zone")}>
                <wos-toggle ?on=${data.useNetworkTimeZone}
                    @toggle=${(e: CustomEvent<{ on: boolean }>) =>
                        service.onNetworkTimeZone(e.detail.on)}></wos-toggle>
            </wos-row>
        </div>
    </div>
    ${data.message ? errorLine(data.message) : ""}
`;

const timezone = (data: DateTimeData, service: DateTimeService) => {
    const list = filterZones(data.zones, data.zoneFilter);
    return html`
        <div class="wos-group">
            <div class="wos-list">
                <wos-field label="" value=${data.zoneFilter} placeholder=${t("Search")}
                    @change=${(e: CustomEvent<{ value: string }>) =>
                        service.onZoneFilter(e.detail.value)}></wos-field>
            </div>
        </div>
        <div class="wos-group">
            <div class="wos-list">
                ${list.length === 0
                    ? html`<wos-row title=${data.busy ? t("Loading...") : t("No time zones")}></wos-row>`
                    : list.map((zone, index) => html`
                        <wos-row title=${zoneLabel(zone)} detail=${zone.Description}
                            @select=${() => service.onPickTimezone(index)}></wos-row>`)}
            </div>
        </div>
        ${data.message ? errorLine(data.message) : ""}
    `;
};

const view = (state: State<DateTimeData>, service: DateTimeService) => {
    const data = state.data;
    const onMain = data.screen === "main";
    return html`
        <div class="wos-card ${onMain ? "" : "dateandtime-has-footer"}">
            <wos-header title=${onMain ? t("Date & Time") : t("Time Zone")}
                       light icon="header-icon-dateandtime.png"
                       ?back=${!onMain} @back=${() => service.onBack()}></wos-header>
            <div class="wos-body">
                ${onMain ? main(data, service) : timezone(data, service)}
            </div>
            ${!onMain ? html`
                <div class="dateandtime-footer">
                    <wos-button class="dateandtime-wide" label=${t("Cancel")}
                        @press=${() => service.onCancelTimezone()}></wos-button>
                </div>` : ""}
            <wos-app-menu ?open=${data.menuOpen}
                .items=${[{ value: "help", label: t("Help") }]}
                @close=${() => service.onMenuChoice("")}
                @choose=${(e: CustomEvent<{ value: string }>) =>
                    service.onMenuChoice(e.detail.value)}></wos-app-menu>
        </div>
    `;
};

const luna = openBus();
const service = createDateTimeService({
    luna,
    setInterval: (callback, ms) => setInterval(callback, ms),
    clearInterval: (handle) => clearInterval(handle as ReturnType<typeof setInterval>),
});
const { app } = startCard({ service, view });
app.on("menu", () => service.onMenu());
