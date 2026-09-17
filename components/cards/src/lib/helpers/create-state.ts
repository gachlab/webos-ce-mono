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
    // Same name, more data.
    patch(data: Partial<Data>, name?: string): void;
    subscribe(listener: Listener<Data>): Unsubscribe;
}

export const createState = <Data>(initial: State<Data>): StateHolder<Data> => {
    let state = initial;
    const listeners = new Set<Listener<Data>>();
    const tell = () => {
        for (const listener of [...listeners]) {
            listener(state);
        }
    };
    return {
        get: () => state,
        set: (next) => {
            state = next;
            tell();
        },
        patch: (data, name) => {
            state = { ...state, ...(name === undefined ? {} : { name }), data: { ...state.data, ...data } };
            tell();
        },
        subscribe: (listener) => {
            listeners.add(listener);
            listener(state);
            return () => listeners.delete(listener);
        },
    };
};
