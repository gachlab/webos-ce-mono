// How a card starts, drawn with our kit. The same five lines in every card,
// written once.
//
//     startCard({
//         service: createWifiService({ luna }),
//         view: (state, service) => html`...`,
//     });
//
// This is the *optional* half. Everything about being a card on this device --
// the lifecycle, stageReady, the back gesture, letting go when the page does --
// is `connectCard` in @webos/api, and a card written in something that is not
// our kit calls that one directly. All this adds is: draw the view with
// lit-html whenever the state changes.
//
// That split is the whole point (#65). enyo's mistake was not having layers,
// it was making the top one compulsory, which is why porting an HP app today
// means rewriting it. Our runtime is 342 lines and it stays optional.
//
// The kit styles itself, at the moment its elements are defined -- see
// kit/kit.ts. Nothing here has to hand it a stylesheet, and a control put on a
// page by anything at all comes out looking right.

import type { AppService } from "@webos/api/infra/app/service.ts";
import type { State } from "@webos/api/helpers/create-state.ts";
import {
    connectCard,
    type CardService,
    type ConnectedCard,
} from "@webos/api/infra/app/connect-card.ts";
import { render, type TemplateResult } from "./element.ts";

// Re-exported because a card that draws with the kit has no reason to know
// which package the contract came from.
export type { CardService } from "@webos/api/infra/app/connect-card.ts";
export type RunningCard = ConnectedCard;

export interface StartCardOptions<Data, Service extends CardService<Data>> {
    readonly service: Service;
    // The card, as a function of its state. It is handed the service itself,
    // so what a control does is `service.onSomething()`.
    readonly view: (state: State<Data>, service: Service) => TemplateResult;
    // Where to draw. The card's own <div id="card"> by default.
    readonly root?: HTMLElement;
    // The card's life. WebAppMgr's by default; a test hands over its own.
    readonly app?: AppService;
}

export const startCard = <Data, Service extends CardService<Data>>(
    options: StartCardOptions<Data, Service>,
): RunningCard => {
    const service = options.service;
    const root = options.root ?? document.getElementById("card") ?? document.body;
    // The generics are named rather than inferred: Data only appears in
    // Service's constraint, which is not an inference site, so TypeScript would
    // settle on unknown and reject paint.
    return connectCard<Data, Service>({
        service,
        paint: (state) => render(options.view(state, service), root),
        // Spread rather than `app: options.app`: with exactOptionalPropertyTypes
        // an explicit undefined is not the same as absent, and absent is what
        // makes connectCard reach for WebAppMgr's.
        ...(options.app ? { app: options.app } : {}),
    });
};
