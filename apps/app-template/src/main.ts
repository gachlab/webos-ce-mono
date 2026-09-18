// The template card: where a card is wired up.
//
// This is the only file that knows what is underneath -- the bus is
// PalmServiceBridge in WebAppMgr and a fake anywhere else -- and everything it
// draws comes from the service's state. `startCard` does the rest: the
// stylesheet, the first frame, telling WebAppMgr the card is ready, and the
// card's own life.

import { openBus } from "@webos/ui-kit/open-bus.ts";
import { createTemplateService, type TemplateData } from "./template.service.ts";
import { html } from "@webos/ui-kit/element.ts";
import { error as errorText, note } from "@webos/ui-kit/kit/kit.ts";
import { startCard } from "@webos/ui-kit/start-card.ts";
import type { State } from "@webos/api/helpers/create-state.ts";
import type { TemplateService } from "./template.service.ts";

const device = (state: State<TemplateData>, service: TemplateService) => html`
    <div class="wos-card">
        <wos-header title="Template"></wos-header>
        <div class="wos-body">
            ${state.name === "template:loading" ? html`<wos-spinner label="Asking the device..."></wos-spinner>` : ""}
            ${state.name === "template:failed" ? errorText(state.error ?? "") : ""}
            ${state.data.device
                ? html`
                    <div class="wos-group">
                        <div class="wos-group-title">This device</div>
                        <div class="wos-list">
                            <wos-row title=${state.data.device.model} detail="Model"></wos-row>
                            <wos-row title=${state.data.device.version || "unknown"} detail="Software"></wos-row>
                            <wos-row title=${state.data.device.serial || "unknown"} detail="Device id"></wos-row>
                        </div>
                    </div>`
                : ""}
            <div class="wos-group">
                <div class="wos-group-title">Network</div>
                <div class="wos-list">
                    <wos-row title=${state.data.connection?.online ? "Online" : "Offline"}
                            detail=${state.data.connection?.through || "Nothing connected"}
                            @select=${() => service.onOpenNetwork()}></wos-row>
                </div>
            </div>
            ${note("This card is the one the others are copied from: the bus, a state machine, two screens and HP's controls.")}
            ${state.name === "template:failed"
                ? html`<div class="wos-group">
                           <wos-button label="Try again" kind="affirmative"
                                      @press=${() => service.onRetry()}></wos-button>
                       </div>`
                : ""}
        </div>
    </div>`;

// The second screen, which the back gesture pops: the card only closes once
// there is nothing left to go back to.
const network = (state: State<TemplateData>, service: TemplateService) => html`
    <div class="wos-card">
        <wos-header title="Network" back @back=${() => service.onBack()}></wos-header>
        <div class="wos-body">
            <div class="wos-group">
                <div class="wos-list">
                    <wos-row title=${state.data.connection?.online ? "Online" : "Offline"} detail="Internet"></wos-row>
                    <wos-row title=${state.data.connection?.through || "none"} detail="Through"></wos-row>
                    <wos-row title=${state.data.connection?.ssid || "-"} detail="Network"></wos-row>
                    <wos-row title=${state.data.connection?.ipAddress || "-"} detail="Address"></wos-row>
                </div>
            </div>
            ${note("It follows the connection while the card is on screen, and stops while it is not.")}
        </div>
    </div>`;

const view = (state: State<TemplateData>, service: TemplateService) =>
    (state.data.screen === "network" ? network : device)(state, service);

startCard({
    service: createTemplateService({ luna: openBus(), log: (message) => console.warn(message) }),
    view,
});
