// The Help settings card: TOC rows that open help.webosarchive.org.

import { createState, type State, type StateHolder, type Unsubscribe } from "@webos/api/helpers/create-state.ts";
import {
    createHelp, HELP_TOPICS, topicUrl, type HelpTopic,
} from "./luna/help.ts";
import { LunaCallError, errorTextOf, type LunaService } from "@webos/api/infra/luna/service.ts";
import type { LaunchParams } from "@webos/api/infra/app/service.ts";

export interface HelpData {
    readonly busy: boolean;
    readonly message: string;
    readonly topics: readonly HelpTopic[];
    readonly menuOpen: boolean;
}

export interface HelpService {
    getState(): State<HelpData>;
    onStateChange(listener: (state: State<HelpData>) => void): Unsubscribe;
    onShown(params?: LaunchParams): void;
    onHidden(): void;
    onBack(): boolean;
    dispose(): void;

    onOpenTopic(id: string): void;
    onMenu(): void;
    onMenuChoice(value: string): void;
}

export const createHelpService = (luna: LunaService): HelpService => {
    const api = createHelp(luna);
    const state: StateHolder<HelpData> = createState<HelpData>({
        name: "help:main",
        data: {
            busy: false,
            message: "",
            topics: HELP_TOPICS,
            menuOpen: false,
        },
    });
    let gone = false;

    const fail = (error: unknown) => {
        if (gone)
            return;
        const message = error instanceof LunaCallError ? errorTextOf(error.reply)
            : error instanceof Error ? error.message : String(error);
        state.patch({ busy: false, message });
    };

    return {
        getState: () => state.get(),
        onStateChange: (listener) => state.subscribe(listener),

        onShown() {
            gone = false;
            state.patch({ message: "" });
        },

        onHidden() {},

        onBack() {
            return false;
        },

        dispose() {
            gone = true;
            state.clear();
        },

        onOpenTopic(id) {
            const topic = state.get().data.topics.find((t) => t.id === id);
            if (!topic)
                return;
            state.patch({ busy: true, message: "" });
            void api.openTarget(topicUrl(topic)).then(() => {
                if (!gone)
                    state.patch({ busy: false });
            }).catch(fail);
        },

        onMenu() {
            state.patch({ menuOpen: true });
        },

        onMenuChoice(value) {
            state.patch({ menuOpen: false });
            if (value === "help")
                void api.openTarget(topicUrl(HELP_TOPICS[0]!)).catch(fail);
        },
    };
};
