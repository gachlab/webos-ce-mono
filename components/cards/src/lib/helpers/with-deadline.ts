// Every wait is bounded, and the bound covers the whole operation -- not only
// the call inside it. A card that waits forever on a service that never answers
// shows a spinner forever.

export const TIMED_OUT: unique symbol = Symbol("timed out");

export type Timers = {
    setTimeout(callback: () => void, ms: number): unknown;
    clearTimeout(handle: unknown): void;
};

export const systemTimers: Timers = {
    setTimeout: (callback, ms) => setTimeout(callback, ms),
    clearTimeout: (handle) => clearTimeout(handle as ReturnType<typeof setTimeout>),
};

export const withDeadline = async <T>(work: Promise<T>, ms: number, onTimeout?: () => void,
                                      timers: Timers = systemTimers): Promise<T | typeof TIMED_OUT> => {
    let handle: unknown;
    const deadline = new Promise<typeof TIMED_OUT>((resolve) => {
        handle = timers.setTimeout(() => resolve(TIMED_OUT), ms);
    });
    try {
        const outcome = await Promise.race([work, deadline]);
        if (outcome === TIMED_OUT) {
            onTimeout?.();
        }
        return outcome;
    } finally {
        timers.clearTimeout(handle);
    }
};

export const timedOut = <T>(outcome: T | typeof TIMED_OUT): outcome is typeof TIMED_OUT => outcome === TIMED_OUT;
