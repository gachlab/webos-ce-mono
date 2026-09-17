// The template card: where a card is wired up.
//
// This is the only file that knows what is underneath -- the bus is
// PalmServiceBridge in WebAppMgr and a fake anywhere else -- and everything it
// draws comes from the service's state. `startCard` does the rest: the
// stylesheet, the first frame, telling WebAppMgr the card is ready, and the
// card's own life.

import { openBus } from "#ui/open-bus.ts";
import { createTemplateService, type TemplateData } from "#lib/services/template.service.ts";
import { html } from "#ui/element.ts";
import { error as errorText, note } from "#ui/kit/kit.ts";
import { startCard } from "#ui/start-card.ts";
import type { State } from "#lib/helpers/create-state.ts";
import type { TemplateService } from "#lib/services/template.service.ts";

const view = (state: State<TemplateData>, service: TemplateService) => html`
    <div class="hp-card">
        <hp-header title="Template"></hp-header>
        <div class="hp-body">
            ${state.name === "template:loading" ? html`<hp-spinner label="Asking the device..."></hp-spinner>` : ""}
            ${state.name === "template:failed" ? errorText(state.error ?? "") : ""}
            ${state.data.device
                ? html`
                    <div class="hp-group">
                        <div class="hp-list">
                            <hp-row title=${state.data.device.model} detail="Model"></hp-row>
                            <hp-row title=${state.data.device.version || "unknown"} detail="Software"></hp-row>
                            <hp-row title=${state.data.device.serial || "unknown"} detail="Device id"></hp-row>
                        </div>
                    </div>`
                : ""}
            ${note("This card is the one the others are copied from: the bus, a state machine and HP's controls.")}
            ${state.name === "template:failed"
                ? html`<div class="hp-group">
                           <hp-button label="Try again" kind="affirmative"
                                      @press=${() => service.onRetry()}></hp-button>
                       </div>`
                : ""}
        </div>
    </div>`;

startCard({
    service: createTemplateService({ luna: openBus(), log: (message) => console.warn(message) }),
    view,
});
