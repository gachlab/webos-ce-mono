// A service's state, and who is watching it.
//
// One shape for every service: a name, the data that goes with it, and an error
// when the name says there is one. The name is part of the contract --
// "wifi:scanning", "wifi:ready" -- and it is what the UI switches on.
//
// A new subscriber hears the current state at once, before anything else
// happens, so a card can paint its first frame without waiting for an event.

export interface State<Data> {
    readonly name: string;
    readonly data: Data;
    readonly error?: string;
}

export type Listener<Data> = (state: State<Data>) => void;
export type Unsubscribe = () => void;

export interface StateHolder<Data> {
    get(): State<Data>;
    set(next: State<Data>): void;
    // Same state, more data. A new name means a new state, so an error from
    // the one before is not carried into it.
    patch(data: Partial<Data>, name?: string): void;
    subscribe(listener: Listener<Data>): Unsubscribe;
    // Lets every subscriber go: what a service does when it is disposed of, so
    // a late answer cannot repaint a card that has closed.
    clear(): void;
}

export const createState = <Data>(initial: State<Data>): StateHolder<Data> => {
    let state = initial;
    const listeners = new Set<Listener<Data>>();
    // The state as it was when the round began: a listener that reacts with a
    // change of its own must not make the listeners after it skip this one.
    const tell = () => {
        const shown = state;
        for (const listener of [...listeners]) {
            listener(shown);
        }
    };
    return {
        get: () => state,
        set: (next) => {
            state = next;
            tell();
        },
        patch: (data, name) => {
            const renamed = name !== undefined && name !== state.name;
            state = {
                name: name ?? state.name,
                data: { ...state.data, ...data },
                // An error belongs to the state it happened in. Kept while the
                // name is the same, gone with it -- otherwise "Wrong password"
                // is still on screen after the card has joined the network.
                ...(renamed || state.error === undefined ? {} : { error: state.error }),
            };
            tell();
        },
        subscribe: (listener) => {
            listeners.add(listener);
            listener(state);
            return () => listeners.delete(listener);
        },
        clear: () => listeners.clear(),
    };
};
