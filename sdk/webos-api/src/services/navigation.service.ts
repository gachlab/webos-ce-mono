// Which screen a card is showing, and how it gets back.
//
// HP's cards are a stack: a row opens a screen, the back gesture pops it, and
// the card closes when there is nothing left to pop. That is the same in every
// settings card, so it is written once, here, rather than invented three times.
//
// A screen is whatever the card says it is -- a name, or a name and what it is
// about -- so this knows nothing about any particular card.

import { createState, type State, type Unsubscribe } from "../helpers/create-state.ts";

export interface Navigation<Screen> {
    now(): Screen;
    stack(): readonly Screen[];
    onChange(listener: (state: State<{ stack: Screen[] }>) => void): Unsubscribe;
    // Opens a screen on top of this one.
    open(screen: Screen): void;
    // Replaces the screen on top, which is what a card does when it is
    // relaunched with a new target rather than opened again.
    show(screen: Screen): void;
    // Goes back. False when there was nowhere to go, which is what tells a
    // card to close.
    back(): boolean;
}

export const createNavigation = <Screen>(first: Screen): Navigation<Screen> => {
    const state = createState<{ stack: Screen[] }>({ name: "screen", data: { stack: [first] } });
    const top = () => {
        const stack = state.get().data.stack;
        return stack[stack.length - 1]!;
    };
    return {
        now: top,
        stack: () => state.get().data.stack,
        onChange: state.subscribe,
        open: (screen) => state.patch({ stack: [...state.get().data.stack, screen] }),
        show: (screen) => state.patch({ stack: [...state.get().data.stack.slice(0, -1), screen] }),
        back: () => {
            const stack = state.get().data.stack;
            if (stack.length <= 1) {
                return false;
            }
            state.patch({ stack: stack.slice(0, -1) });
            return true;
        },
    };
};
