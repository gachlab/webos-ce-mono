// How a card starts. The same five lines in every card, written once.
//
//     startCard({
//         service: createWifiService({ luna }),
//         view: (state, service) => html`...`,
//     });
//
// What it does, and what a card would otherwise forget:
//
//   * hands kit.css to the elements and draws on every change of state;
//   * tells WebAppMgr the card is ready, which is what gets it on screen;
//   * gives the service the card's own life -- shown, hidden, relaunched --
//     so a subscription can be dropped while the card is away instead of
//     running against a card nobody is looking at;
//   * lets go of everything when the page does.

import styles from "#ui/kit.css";
import type { AppService, LaunchParams } from "#lib/infra/app/service.ts";
import type { State } from "#lib/helpers/create-state.ts";
import { createPalmSystemApp } from "#lib/infra/app/palm-system.service.ts";
import { render, useStyles, type TemplateResult } from "#ui/element.ts";

// What a card's service has to offer for the card to be started. Everything
// else about it is the card's business.
export interface CardService<Data> {
    getState(): State<Data>;
    onStateChange(listener: (state: State<Data>) => void): () => void;
    // The card is on screen, or has come back to it.
    onShown?(params: LaunchParams): void;
    // It was sent away: a good moment to stop watching a service.
    onHidden?(): void;
    // The back gesture. True when the card handled it; false, or nothing at
    // all, closes the card, as HP's do.
    onBack?(): boolean;
    dispose?(): void;
}

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

export interface RunningCard {
    // The card's life, for whatever the card wants beyond shown and hidden:
    // the keyboard, a relaunch it wants to read itself, a banner.
    readonly app: AppService;
    readonly stop: () => void;
}

export const startCard = <Data, Service extends CardService<Data>>(
    options: StartCardOptions<Data, Service>,
): RunningCard => {
    useStyles(styles);
    const app = options.app ?? createPalmSystemApp({ window: globalThis as never });
    const service = options.service;
    const root = options.root ?? document.getElementById("card") ?? document.body;

    const stops = [
        service.onStateChange((state) => render(options.view(state, service), root)),
        app.on("activated", () => service.onShown?.(app.launchParams())),
        app.on("deactivated", () => service.onHidden?.()),
        app.on("relaunched", (params) => service.onShown?.(params)),
        app.on("back", () => {
            if (!service.onBack?.()) {
                app.close();
            }
        }),
    ];

    service.onShown?.(app.launchParams());
    // Last: WebAppMgr shows the card as soon as this is said, and what it
    // shows should be the first frame, not an empty page.
    app.ready();

    const stop = () => {
        for (const release of stops.splice(0)) {
            release();
        }
        service.dispose?.();
        app.dispose();
    };
    globalThis.addEventListener?.("unload", stop, { once: true });
    return { app, stop };
};
