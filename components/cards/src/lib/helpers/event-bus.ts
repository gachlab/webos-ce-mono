// What one service says to another. Services never import each other: they say
// what happened, and whoever cares is listening.

export type BusListener = (payload?: unknown) => void;

export interface EventBus {
    on(event: string, listener: BusListener): () => void;
    emit(event: string, payload?: unknown): void;
    clear(): void;
}

export const createEventBus = (): EventBus => {
    const listeners = new Map<string, Set<BusListener>>();
    return {
        on: (event, listener) => {
            const forEvent = listeners.get(event) ?? new Set<BusListener>();
            forEvent.add(listener);
            listeners.set(event, forEvent);
            return () => {
                forEvent.delete(listener);
            };
        },
        emit: (event, payload) => {
            for (const listener of [...(listeners.get(event) ?? [])]) {
                listener(payload);
            }
        },
        clear: () => listeners.clear(),
    };
};
