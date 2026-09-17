// The card's life, as WebAppMgr runs it.
//
// WebAppMgr calls functions it expects to find on the page -- window.Mojo --
// and answers questions through window.PalmSystem. This is the only place that
// knows those names:
//
//   Mojo.stageActivated()      the card came to the front
//   Mojo.stageDeactivated()    it was sent away
//   Mojo.relaunch()            launched again; the new parameters are in
//                              PalmSystem.launchParams by then
//   Mojo.keyboardShown(bool)   the virtual keyboard took part of the screen
//   Mojo.handleGesture(name)   "back" among them; Escape arrives as a key
//
// What was already on window.Mojo is kept and called too: HP's own framework
// puts its handlers there, and a card of ours may be opened beside one of its
// pages.

import type { AppEvent, AppEvents, AppService, LaunchParams } from "./service.ts";

interface PalmSystem {
    launchParams?: string;
    identifier?: string;
    locale?: string;
    stageReady?: () => void;
    activate?: () => void;
    deactivate?: () => void;
    addBannerMessage?: (message: string, params: string, icon?: string, soundClass?: string) => string;
}

interface MojoHooks {
    [name: string]: unknown;
    stageActivated?: () => void;
    stageDeactivated?: () => void;
    relaunch?: () => void;
    keyboardShown?: (shown: boolean) => void;
    handleGesture?: (name: string, detail?: unknown) => void;
}

const parseParams = (text: string | undefined): LaunchParams => {
    if (!text) {
        return {};
    }
    try {
        const value: unknown = JSON.parse(text);
        return value !== null && typeof value === "object" ? value as LaunchParams : {};
    } catch {
        return {};
    }
};

export interface PalmSystemDeps {
    // The page. Taken as an argument so a test can hand over its own.
    readonly window: Record<string, unknown> & {
        addEventListener?: Window["addEventListener"];
        removeEventListener?: Window["removeEventListener"];
    };
    // The clock, for the one thing that needs it: telling one back from two.
    readonly now?: () => number;
}

export const createPalmSystemApp = (deps: PalmSystemDeps): AppService => {
    const page = deps.window;
    const now = deps.now ?? (() => Date.now());
    const system = () => (page.PalmSystem ?? {}) as PalmSystem;
    const listeners = new Map<AppEvent, Set<(...args: never[]) => void>>();

    const tell = <E extends AppEvent>(event: E, ...args: Parameters<AppEvents[E]>) => {
        for (const listener of [...(listeners.get(event) ?? [])]) {
            (listener as (...rest: unknown[]) => void)(...args);
        }
    };

    // Whatever was there stays there: HP's framework may own it, and a hook
    // that is replaced rather than wrapped takes its app down with it.
    const previous = (page.Mojo ?? {}) as MojoHooks;
    const hooks: MojoHooks = {
        ...previous,
        stageActivated: () => {
            previous.stageActivated?.();
            tell("activated");
        },
        stageDeactivated: () => {
            previous.stageDeactivated?.();
            tell("deactivated");
        },
        relaunch: () => {
            previous.relaunch?.();
            tell("relaunched", parseParams(system().launchParams));
        },
        keyboardShown: (shown: boolean) => {
            previous.keyboardShown?.(shown);
            tell("keyboard", shown);
        },
        handleGesture: (name: string, detail?: unknown) => {
            previous.handleGesture?.(name, detail);
            if (name === "back") {
                back(now());
            }
        },
    };
    page.Mojo = hooks;

    // Back arrives twice on a device with a keyboard: as the gesture and as
    // the key. One back is one back.
    let lastBack = -1;
    const back = (at: number) => {
        if (at - lastBack < 300) {
            return;
        }
        lastBack = at;
        tell("back");
    };

    // The back key, which is Escape here and on the device's keyboard both.
    const onKey = ((event: KeyboardEvent) => {
        if (event.key === "Escape") {
            back(now());
        }
    }) as EventListener;
    page.addEventListener?.("keydown", onKey);

    return {
        launchParams: () => parseParams(system().launchParams),
        identifier: () => system().identifier ?? "",
        locale: () => system().locale ?? "en_US",
        ready: () => system().stageReady?.(),
        close: () => {
            // HP's cards close by closing their window; PalmSystem has no
            // "close me" of its own.
            (page.close as (() => void) | undefined)?.();
        },
        banner: (message: string) => {
            system().addBannerMessage?.(message, "{}");
        },
        dispose: () => {
            page.removeEventListener?.("keydown", onKey);
            page.Mojo = previous;
            listeners.clear();
        },
        on: (event, listener) => {
            const forEvent = listeners.get(event) ?? new Set();
            forEvent.add(listener as (...args: never[]) => void);
            listeners.set(event, forEvent);
            return () => {
                forEvent.delete(listener as (...args: never[]) => void);
            };
        },
    };
};
