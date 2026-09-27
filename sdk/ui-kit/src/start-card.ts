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
// means rewriting it. Our renderer is 316 lines and it stays optional.
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
    const card = connectCard<Data, Service>({
        service,
        paint: (state) => render(options.view(state, service), root),
        // Spread rather than `app: options.app`: with exactOptionalPropertyTypes
        // an explicit undefined is not the same as absent, and absent is what
        // makes connectCard reach for WebAppMgr's.
        ...(options.app ? { app: options.app } : {}),
    });
    // The keyboard, and the field it would otherwise sit on top of. WebAppMgr
    // says the keyboard took part of the screen (AppService's `keyboard`); the
    // card's body is the only thing that scrolls, so it is the thing to make
    // room in. This is the kit's business and not connectCard's: connectCard
    // touches no DOM, and a card written in something else hears the same
    // event and answers it its own way. See page.css, §0, --wos-keyboard.
    watchKeyboard(card.app);
    return card;
};

// The screen the virtual keyboard covers is a fixed fraction of the viewport
// on HP's devices; the exact pixels are not reported, so this is the reserve a
// field needs to clear it. When the keyboard is up the body grows its bottom
// padding by this, and whatever has the focus is scrolled above the fold.
const KEYBOARD_RESERVE = "50vh";

const watchKeyboard = (app: RunningCard["app"]): void => {
    if (typeof document === "undefined") {
        return;
    }
    app.on("keyboard", (shown) => {
        document.documentElement.style.setProperty(
            "--wos-keyboard",
            shown ? KEYBOARD_RESERVE : "0px",
        );
        if (shown) {
            // Let the padding land, then bring the focused field into view
            // above the keyboard. A field inside a control's shadow root
            // reports itself through activeElement chains, so the deepest one
            // is what has to clear the fold.
            requestAnimationFrame(() => focusedField()?.scrollIntoView({ block: "center" }));
        }
    });
};

// The element that actually has the caret, following the focus down through
// any shadow roots a control drew.
const focusedField = (): Element | null => {
    let active: Element | null = document.activeElement;
    while (active?.shadowRoot?.activeElement) {
        active = active.shadowRoot.activeElement;
    }
    return active;
};
