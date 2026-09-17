// A subscription that follows the card: on while it is being looked at, off
// while it is not.
//
// A card that is sent away keeps its page and its service; what it must not
// keep is a service pushing at it. Every card would otherwise write the same
// start/stop by hand -- and forget the stop, which is the one that matters.

import type { Subscription } from "#lib/infra/luna/service.ts";

export interface Watch {
    start(): void;
    stop(): void;
    readonly watching: () => boolean;
}

export const createWatch = (subscribe: () => Subscription): Watch => {
    let open: Subscription | undefined;
    return {
        start: () => {
            if (!open) {
                open = subscribe();
            }
        },
        stop: () => {
            open?.cancel();
            open = undefined;
        },
        watching: () => open !== undefined,
    };
};
