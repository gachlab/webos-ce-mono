// The Print Manager card.
//
// One screen, following HP's Print Manager (spec, not code): the printers CUPS
// knows, the current one marked, tap to make another the default. The print
// dialog (enyo's PrintDialog) drives the actual print flow and its own progress
// popup; this card is where the default printer is chosen and the queue is seen.

import { openBus } from "@webos/api/infra/luna/open-bus.ts";
import { t } from "@webos/api/i18n/translate.ts";
import { html } from "@webos/ui-kit/element.ts";
import { error as errorLine, note } from "@webos/ui-kit/kit/kit.ts";
import { startCard } from "@webos/ui-kit/start-card.ts";
import {
    createPrintService, type PrintData, type PrintService,
} from "./printmanager.service.ts";
import type { Printer } from "./luna/printmgr.ts";
import type { State } from "@webos/api/helpers/create-state.ts";

// Onyx checkmark.png (Apache, from enyo in this tree), as the other cards use
// for the chosen item.
const tick = () => html`
    <img class="pm-current" src="images/checkmark.png" alt=${t("Default printer")}>`;

const printerRow = (printer: Printer, data: PrintData, service: PrintService) => {
    const current = data.currentId === printer.id;
    return html`
        <wos-row
            title=${printer.name}
            detail=${printer.address}
            ?strong=${current}
            @select=${() => service.onSelectPrinter(printer.id)}>
            ${current ? tick() : ""}
        </wos-row>`;
};

const list = (data: PrintData, service: PrintService) => {
    if (data.printers.length === 0) {
        return html`
            ${data.searching
                ? note(t("Looking for printers..."))
                : note(t("No printers found. Connect a printer on your network to print."))}
            ${data.message ? errorLine(data.message) : ""}
        `;
    }
    return html`
        <div class="wos-group">
            <div class="wos-group-title">${t("Printers")}</div>
            <div class="wos-list">
                ${data.printers.map((printer) => printerRow(printer, data, service))}
            </div>
        </div>
        ${data.message ? errorLine(data.message) : ""}
    `;
};

const view = (state: State<PrintData>, service: PrintService) => {
    const data = state.data;
    return html`
        <div class="wos-card">
            <wos-header title=${t("Print Manager")} light icon="header-icon-print.png"></wos-header>
            <div class="wos-body">${list(data, service)}</div>
        </div>
    `;
};

const luna = openBus();
const service = createPrintService(luna);
startCard({ service, view });
