// Which bus a card talks to.
//
// In WebAppMgr it is PalmServiceBridge. In a plain browser -- where a screen is
// written and looked at without a session -- there is none, and a card would
// show nothing but its failure. `WEBOS_CARDS_DEV` is replaced by the bundler
// (tools/build-cards.sh) with false for what is installed, so the fake bus and
// whatever a card answers with in development are not in the shipped card.

import { createBridgeLuna, openPalmServiceBridge } from "./bridge.service.ts";
import { createFakeLuna } from "./fake.service.ts";
import type { LunaService, Payload } from "./service.ts";

// tools/build-cards.sh replaces this. A card bundled by anything else -- which
// is the whole point of the platform being a package -- simply gets the real
// bridge, rather than a ReferenceError on the first line of openBus().
declare const WEBOS_CARDS_DEV: boolean | undefined;
const devBuild = (): boolean => typeof WEBOS_CARDS_DEV !== "undefined" && WEBOS_CARDS_DEV;

export type DevAnswers = Record<string, (payload: Payload) => Payload>;

export const openBus = (answers: DevAnswers = {}): LunaService => {
    if (!devBuild() || (globalThis as { PalmServiceBridge?: unknown }).PalmServiceBridge) {
        return createBridgeLuna({ open: openPalmServiceBridge, log: (message) => console.warn(message) });
    }
    // Only reached in a development build, and only in a browser. The
    // bundler drops this branch, and the fake with it, when WEBOS_CARDS_DEV
    // is false.
    const fake = createFakeLuna();
    for (const [uri, answer] of Object.entries(answers)) {
        fake.answer(uri, answer);
    }
    return fake;
};
