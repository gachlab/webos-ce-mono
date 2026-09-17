// Which bus a card talks to.
//
// In WebAppMgr it is PalmServiceBridge. In a plain browser -- where a screen is
// written and looked at without a session -- there is none, and a card would
// show nothing but its failure. `WEBOS_CARDS_DEV` is replaced by the bundler
// (tools/build-cards.sh) with false for what is installed, so the fake bus and
// whatever a card answers with in development are not in the shipped card.

import { createBridgeLuna, openPalmServiceBridge } from "#lib/infra/luna/bridge.service.ts";
import { createFakeLuna } from "#lib/infra/luna/fake.service.ts";
import type { LunaService, Payload } from "#lib/infra/luna/service.ts";

declare const WEBOS_CARDS_DEV: boolean;

export type DevAnswers = Record<string, (payload: Payload) => Payload>;

export const openBus = (answers: DevAnswers = {}): LunaService => {
    if (!WEBOS_CARDS_DEV || (globalThis as { PalmServiceBridge?: unknown }).PalmServiceBridge) {
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
