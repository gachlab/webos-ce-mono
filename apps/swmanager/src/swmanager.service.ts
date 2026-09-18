// The Software Manager settings card: installed apps, details, remove.

import { createState, type State, type StateHolder, type Unsubscribe } from "@webos/api/helpers/create-state.ts";
import { createNavigation } from "@webos/api/services/navigation.service.ts";
import { createSwManager, type InstalledApp } from "./luna/swmanager.ts";
import { LunaCallError, errorTextOf, type LunaService } from "@webos/api/infra/luna/service.ts";
import type { LaunchParams } from "@webos/api/infra/app/service.ts";

export type SwManagerScreen = "list" | "details";

export interface SwManagerData {
    readonly screen: SwManagerScreen;
    readonly busy: boolean;
    readonly message: string;
    readonly apps: InstalledApp[];
    readonly details?: InstalledApp | undefined;
    readonly confirmDelete?: boolean | undefined;
    readonly menuOpen: boolean;
}

export interface SwManagerService {
    getState(): State<SwManagerData>;
    onStateChange(listener: (state: State<SwManagerData>) => void): Unsubscribe;
    onShown(params?: LaunchParams): void;
    onHidden(): void;
    onBack(): boolean;
    dispose(): void;

    onOpenDetails(id: string): void;
    onAskDelete(): void;
    onConfirmDelete(value: string): void;
    onMenu(): void;
    onMenuChoice(value: string): void;
}

export const HELP_URL = "https://help.webosarchive.org/en-us/";

export const createSwManagerService = (luna: LunaService): SwManagerService => {
    const api = createSwManager(luna);
    const screens = createNavigation<SwManagerScreen>("list");
    const state: StateHolder<SwManagerData> = createState<SwManagerData>({
        name: "swmanager:list",
        data: {
            screen: "list",
            busy: false,
            message: "",
            apps: [],
            menuOpen: false,
        },
    });
    let gone = false;

    const show = (screen: SwManagerScreen) => {
        if (screens.now() !== screen)
            screens.open(screen);
        state.patch({ screen }, `swmanager:${screen}`);
    };

    screens.onChange(() => state.patch({ screen: screens.now() }, `swmanager:${screens.now()}`));

    const fail = (error: unknown) => {
        if (gone)
            return;
        const message = error instanceof LunaCallError ? errorTextOf(error.reply)
            : error instanceof Error ? error.message : String(error);
        state.patch({ busy: false, message, confirmDelete: undefined });
    };

    const load = async () => {
        state.patch({ busy: true, message: "" });
        try {
            const apps = await api.listApps();
            if (gone)
                return;
            const details = state.get().data.details;
            const updated = details
                ? apps.find((a) => a.id === details.id)
                : undefined;
            state.patch({
                busy: false,
                apps,
                ...(updated ? { details: updated } : {}),
            });
        } catch (error) {
            fail(error);
        }
    };

    return {
        getState: () => state.get(),
        onStateChange: (listener) => state.subscribe(listener),

        onShown() {
            gone = false;
            void load();
        },

        onHidden() {},

        onBack() {
            if (screens.now() === "list")
                return false;
            if (!screens.back())
                return false;
            state.patch({
                screen: "list",
                details: undefined,
                confirmDelete: undefined,
                message: "",
            }, "swmanager:list");
            return true;
        },

        dispose() {
            gone = true;
            state.clear();
        },

        onOpenDetails(id) {
            const app = state.get().data.apps.find((a) => a.id === id);
            if (!app)
                return;
            state.patch({ details: app, confirmDelete: undefined, message: "" });
            show("details");
        },

        onAskDelete() {
            const details = state.get().data.details;
            if (!details?.removable) {
                state.patch({
                    message: "This application cannot be deleted from your device.",
                });
                return;
            }
            state.patch({ confirmDelete: true });
        },

        onConfirmDelete(value) {
            const details = state.get().data.details;
            state.patch({ confirmDelete: undefined });
            if (!details || value !== "ok" || !details.removable)
                return;
            state.patch({ busy: true, message: "" });
            void api.remove(details.id).then(() => {
                if (gone)
                    return;
                while (screens.back())
                    ;
                state.patch({
                    screen: "list",
                    details: undefined,
                    busy: false,
                }, "swmanager:list");
                void load();
            }).catch(fail);
        },

        onMenu() {
            state.patch({ menuOpen: true });
        },

        onMenuChoice(value) {
            state.patch({ menuOpen: false });
            if (value === "help")
                void api.openTarget(HELP_URL).catch(fail);
        },
    };
};
