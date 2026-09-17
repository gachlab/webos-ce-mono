// The clock, as something that can be handed over.
//
// Every wait in the library goes through this, so a test can move time itself
// rather than sleep. The bridge is the only user today (its call and the first
// reply of a subscription are bounded); a service that waits for anything else
// takes the same type.

export interface Timers {
    setTimeout(callback: () => void, ms: number): unknown;
    clearTimeout(handle: unknown): void;
}

export const systemTimers: Timers = {
    setTimeout: (callback, ms) => setTimeout(callback, ms),
    clearTimeout: (handle) => clearTimeout(handle as ReturnType<typeof setTimeout>),
};
