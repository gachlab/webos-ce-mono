// The card's own life: the port.
//
// A webOS card is not a page that happens to be open. WebAppMgr launches it
// with parameters, brings it to the front and sends it away, relaunches it with
// new parameters instead of opening a second one, tells it the keyboard took
// half the screen, and waits to be told the card is ready before it shows
// anything. All of that arrives as calls on a global `Mojo` object and as
// `PalmSystem`; none of it belongs in a card's code.

export interface LaunchParams {
    readonly [key: string]: unknown;
}

export interface AppEvents {
    // The card was brought to the front, or sent away.
    activated(): void;
    deactivated(): void;
    // Launched again while it was already open, with these parameters. HP's
    // apps switch screen here instead of opening a second card.
    relaunched(params: LaunchParams): void;
    // The virtual keyboard took part of the screen, or gave it back.
    keyboard(shown: boolean): void;
    // The back gesture, or Escape.
    back(): void;
}

export type AppEvent = keyof AppEvents;

export interface AppService {
    // What the card was launched with.
    launchParams(): LaunchParams;
    // The card's id, and the locale to show it in.
    identifier(): string;
    locale(): string;
    // The card is drawn and can be shown. WebAppMgr holds a card off screen
    // until this is called (see stageReady in PalmSystem).
    ready(): void;
    // Close this card, as the app's own "Done" does.
    close(): void;
    // HP's banner, the one-line message across the top of the screen.
    banner(message: string): void;
    on<E extends AppEvent>(event: E, listener: AppEvents[E]): () => void;
}
