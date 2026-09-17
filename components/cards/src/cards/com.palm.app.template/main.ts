// The template card: where a card is wired up.
//
// This is the only file that knows what is underneath -- the bus is
// PalmServiceBridge in WebAppMgr and a fake anywhere else -- and the only one
// that reaches for the page. Everything it draws comes from the service's
// state.

import styles from "#ui/hp.css";
import { createBridgeLuna, openPalmServiceBridge } from "#lib/infra/luna/bridge.service.ts";
import { createFakeLuna } from "#lib/infra/luna/fake.service.ts";
import { createTemplateService, type TemplateData } from "#lib/services/template.service.ts";
import type { LunaService } from "#lib/infra/luna/service.ts";
import { html, render, useStyles } from "#ui/element.ts";
import { error as errorText, note } from "#ui/kit/kit.ts";
import type { State } from "#lib/helpers/create-state.ts";

// WebAppMgr's bus, or a fake one in a plain browser, so a card can be opened
// and looked at while it is being written.
const openBus = (): LunaService => {
    if ((globalThis as { PalmServiceBridge?: unknown }).PalmServiceBridge) {
        return createBridgeLuna({ open: openPalmServiceBridge, log: (message) => console.warn(message) });
    }
    const fake = createFakeLuna();
    fake.answer("luna://com.palm.deviceprofile/getDeviceProfile", () => ({
        returnValue: true,
        deviceInfo: { deviceModel: "A browser", softwareVersion: "no WebAppMgr here", nduId: "-" },
    }));
    return fake;
};

const view = (state: State<TemplateData>, service: { onRetry(): void }) => html`
    <div class="hp-card">
        <hp-header title="Template"></hp-header>
        <div class="hp-body">
            ${state.name === "template:loading" ? html`<hp-spinner label="Asking the device..."></hp-spinner>` : ""}
            ${state.name === "template:failed" ? errorText(state.error ?? "") : ""}
            ${state.data.device
                ? html`
                    <div class="hp-group">
                        <hp-row title=${state.data.device.model} detail="Model"></hp-row>
                        <hp-row title=${state.data.device.version || "unknown"} detail="Software"></hp-row>
                        <hp-row title=${state.data.device.serial || "unknown"} detail="Device id"></hp-row>
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

const start = () => {
    useStyles(styles);
    const service = createTemplateService({ luna: openBus(), log: (message) => console.warn(message) });
    const root = document.getElementById("card") ?? document.body;
    service.onStateChange((state) => render(view(state, service), root));
    service.onShown();
    // WebAppMgr shows a card only once its page says it is ready.
    (globalThis as { PalmSystem?: { stageReady?: () => void } }).PalmSystem?.stageReady?.();
};

start();
