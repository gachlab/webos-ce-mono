// The card every other card is copied from: two screens, one state machine.
//
// It asks com.palm.deviceprofile who this device is, watches
// com.palm.connectionmanager while it is being looked at, and opens a second
// screen for the details. That is everything a card does: ask the bus, wait,
// show what came back, say so when nothing did, let the user try again, open a
// screen and come back from it, and stop watching when it is sent away.
//
// The shape is the one every page service follows:
//   * the state names are "<screen>:<phase>" and are part of the contract;
//   * everything is a function, and what it needs arrives in `deps`;
//   * the UI reads `state` and calls `onSomething()`, and knows nothing else.

import { createState, type State, type StateHolder, type Unsubscribe } from "#lib/helpers/create-state.ts";
import { createWatch } from "#lib/helpers/watch.ts";
import { createConnectionManager, type ConnectionStatus } from "#lib/infra/luna/connectionmanager.ts";
import { createNavigation } from "#lib/services/navigation.service.ts";
import { LunaCallError, errorTextOf, type LunaService, type Payload } from "#lib/infra/luna/service.ts";

export interface DeviceFacts {
    readonly model: string;
    readonly version: string;
    readonly serial: string;
}

export type TemplateScreen = "device" | "network";

export interface TemplateData {
    readonly screen: TemplateScreen;
    readonly device?: DeviceFacts;
    readonly connection?: ConnectionStatus;
}

export type TemplateStateName = "template:loading" | "template:ready" | "template:failed";

export interface TemplateService {
    getState(): State<TemplateData>;
    onStateChange(listener: (state: State<TemplateData>) => void): Unsubscribe;
    // The card is on screen, or has come back to it.
    onShown(): void;
    // It was sent away: what the bus is pushing is no longer worth hearing.
    onHidden(): void;
    // The user asked again after a failure.
    onRetry(): void;
    // The second screen, and the way back from it.
    onOpenNetwork(): void;
    onBack(): boolean;
    dispose(): void;
}

const DEVICE_PROFILE = "luna://com.palm.deviceprofile/getDeviceProfile";

// What the reply says, in the fields this card shows. A missing field is
// empty, never "undefined" on screen.
export const factsOf = (reply: Payload): DeviceFacts => {
    const info = (reply.deviceInfo ?? {}) as Payload;
    const text = (value: unknown): string => (typeof value === "string" ? value : "");
    return {
        model: text(info.deviceModel) || "webOS device",
        version: text(info.softwareVersion),
        serial: text(info.nduId),
    };
};

export interface TemplateDeps {
    readonly luna: LunaService;
    readonly log?: (message: string) => void;
}

export const createTemplateService = (deps: TemplateDeps): TemplateService => {
    const state: StateHolder<TemplateData> = createState<TemplateData>({
        name: "template:loading",
        data: { screen: "device" },
    });
    const screens = createNavigation<TemplateScreen>("device");
    const connection = createConnectionManager(deps.luna);
    let asking = false;
    let gone = false;

    screens.onChange(() => state.patch({ screen: screens.now() }));

    // On while the card is being looked at, off while it is not.
    const watch = createWatch(() => connection.watchStatus(
        (status) => {
            if (!gone) {
                state.patch({ connection: status });
            }
        },
        (error) => deps.log?.(error.message),
    ));

    const ask = async () => {
        if (asking || gone) {
            return;
        }
        asking = true;
        state.set({ name: "template:loading", data: state.get().data });
        try {
            const reply = await deps.luna.call(DEVICE_PROFILE);
            // The card may have closed while the bus was answering; a reply
            // that arrives then has nowhere to go.
            if (gone) {
                return;
            }
            state.set({ name: "template:ready", data: { ...state.get().data, device: factsOf(reply) } });
        } catch (error) {
            if (gone) {
                return;
            }
            // What the user reads is what the service said; the uri and the
            // rest go to the log, where they are of some use.
            const text = error instanceof LunaCallError ? errorTextOf(error.reply)
                : error instanceof Error ? error.message : String(error);
            deps.log?.(error instanceof Error ? error.message : String(error));
            state.set({ name: "template:failed", data: state.get().data, error: text });
        } finally {
            asking = false;
        }
    };

    return {
        getState: state.get,
        onStateChange: state.subscribe,
        onShown: () => {
            watch.start();
            void ask();
        },
        onHidden: () => watch.stop(),
        onRetry: () => void ask(),
        onOpenNetwork: () => screens.open("network"),
        onBack: () => screens.back(),
        dispose: () => {
            gone = true;
            asking = false;
            watch.stop();
            state.clear();
        },
    };
};
