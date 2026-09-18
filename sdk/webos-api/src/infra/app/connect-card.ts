// Connecting a card to the device. Half of what starting a card means, and the
// half that has nothing to do with how it is drawn.
//
//     const { app, stop } = connectCard({
//         service: createWifiService({ luna: openBus() }),
//         paint: (state) => myFrameworkRenders(state),
//     });
//
// What it does, and what a card would otherwise forget:
//
//   * tells WebAppMgr the card is ready, which is what gets it on screen --
//     and says it last, so what appears is the first frame and not a blank
//     page;
//   * gives the service the card's own life: shown, hidden, relaunched, so a
//     subscription can be dropped while the card is away instead of running
//     against a card nobody is looking at;
//   * turns the back gesture into HP's behaviour -- the card handles it, or
//     the card closes;
//   * lets go of everything when the page does.
//
// `paint` is the seam. The platform says "the state is now this"; what draws
// is the caller's business, and it can be our kit (see @webos/ui-kit's
// startCard, which is this function plus a lit-html render), React, an enyo
// shim (#56), or nothing at all.
//
// Nothing here touches the DOM. That is the point: a card for this device can
// be written in anything, and this is the part it cannot do without.

import type { AppService, LaunchParams } from "./service.ts";
import { createPalmSystemApp } from "./palm-system.service.ts";
import type { State } from "../../helpers/create-state.ts";

// What a card's service has to offer for the card to be connected. Everything
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

export interface ConnectCardOptions<Data, Service extends CardService<Data>> {
    readonly service: Service;
    // Called with the card's state at once, and again on every change. This is
    // where a renderer hooks in; leave it out and the card draws nothing,
    // which is a legitimate thing for a card that only talks to the bus.
    readonly paint?: (state: State<Data>) => void;
    // The card's life. WebAppMgr's by default; a test hands over its own.
    readonly app?: AppService;
}

export interface ConnectedCard {
    // The card's life, for whatever the card wants beyond shown and hidden:
    // the keyboard, a relaunch it wants to read itself, a banner.
    readonly app: AppService;
    readonly stop: () => void;
}

export const connectCard = <Data, Service extends CardService<Data>>(
    options: ConnectCardOptions<Data, Service>,
): ConnectedCard => {
    const app = options.app ?? createPalmSystemApp({ window: globalThis as never });
    const service = options.service;

    // Subscribing paints at once -- createState hands the current state over on
    // subscribe -- so the first frame is on the page before ready() is said.
    const stops: Array<() => void> = [];
    if (options.paint) {
        stops.push(service.onStateChange(options.paint));
    }
    stops.push(
        app.on("activated", () => service.onShown?.(app.launchParams())),
        app.on("deactivated", () => service.onHidden?.()),
        app.on("relaunched", (params) => service.onShown?.(params)),
        app.on("back", () => {
            if (!service.onBack?.()) {
                app.close();
            }
        }),
    );

    service.onShown?.(app.launchParams());
    // Last: WebAppMgr shows the card as soon as this is said.
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
