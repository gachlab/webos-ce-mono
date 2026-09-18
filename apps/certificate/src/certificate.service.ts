// The Certificate Manager settings card: list, details, and trust (add).
//
// Screens and wording follow HP's Certificate Manager on the TouchPad CE image
// (spec, not code).

import { createState, type State, type StateHolder, type Unsubscribe } from "@webos/api/helpers/create-state.ts";
import { createNavigation } from "@webos/api/services/navigation.service.ts";
import { createCertificate, type Certificate } from "./luna/certificate.ts";
import { LunaCallError, errorTextOf, type LunaService } from "@webos/api/infra/luna/service.ts";
import type { LaunchParams } from "@webos/api/infra/app/service.ts";

export type CertificateScreen = "list" | "details" | "add";

export interface CertificateData {
    readonly screen: CertificateScreen;
    readonly busy: boolean;
    readonly message: string;
    readonly certificates: Certificate[];
    readonly details?: Certificate | undefined;
    readonly swipeOpen?: string | undefined;
    readonly path: string;
    readonly passphrase: string;
    readonly menuOpen: boolean;
}

export interface CertificateService {
    getState(): State<CertificateData>;
    onStateChange(listener: (state: State<CertificateData>) => void): Unsubscribe;
    onShown(params?: LaunchParams): void;
    onHidden(): void;
    onBack(): boolean;
    dispose(): void;

    onOpenDetails(id: string): void;
    onOpenAdd(): void;
    onSwipe(id: string, open: boolean): void;
    onDelete(id: string): void;
    onAddField(change: Partial<{ path: string; passphrase: string }>): void;
    onTrust(): void;
    onCancelAdd(): void;
    onMenu(): void;
    onMenuChoice(value: string): void;
}

export const HELP_URL = "https://help.webosarchive.org/en-us/";

export const createCertificateService = (luna: LunaService): CertificateService => {
    const api = createCertificate(luna);
    const screens = createNavigation<CertificateScreen>("list");
    const state: StateHolder<CertificateData> = createState<CertificateData>({
        name: "certificate:list",
        data: {
            screen: "list",
            busy: false,
            message: "",
            certificates: [],
            path: "",
            passphrase: "",
            menuOpen: false,
        },
    });
    let gone = false;

    const show = (screen: CertificateScreen) => {
        if (screens.now() !== screen)
            screens.open(screen);
        state.patch({ screen }, `certificate:${screen}`);
    };

    screens.onChange(() => state.patch({ screen: screens.now() }, `certificate:${screens.now()}`));

    const fail = (error: unknown) => {
        if (gone)
            return;
        const message = error instanceof LunaCallError ? errorTextOf(error.reply)
            : error instanceof Error ? error.message : String(error);
        state.patch({ busy: false, message });
    };

    const openList = async () => {
        while (screens.back())
            ;
        state.patch({ busy: true, message: "", swipeOpen: undefined, details: undefined });
        try {
            const certificates = await api.list();
            if (gone)
                return;
            state.set({
                name: "certificate:list",
                data: {
                    screen: "list",
                    busy: false,
                    message: "",
                    certificates,
                    path: "",
                    passphrase: "",
                    menuOpen: false,
                },
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
            void openList();
        },

        onHidden() {},

        onBack() {
            if (screens.now() === "list")
                return false;
            if (!screens.back())
                return false;
            const screen = screens.now();
            if (screen === "list") {
                void openList();
                return true;
            }
            state.patch({ screen, message: "" }, `certificate:${screen}`);
            return true;
        },

        dispose() {
            gone = true;
            state.clear();
        },

        onOpenDetails(id) {
            state.patch({ busy: true, message: "", swipeOpen: undefined });
            void api.details(id).then((details) => {
                if (gone)
                    return;
                state.patch({ details, busy: false });
                show("details");
            }).catch(fail);
        },

        onOpenAdd() {
            state.patch({ path: "", passphrase: "", message: "" });
            show("add");
        },

        onSwipe(id, open) {
            state.patch({ swipeOpen: open ? id : undefined });
        },

        onDelete(id) {
            state.patch({ busy: true, message: "", swipeOpen: undefined });
            void api.remove(id).then(() => {
                if (!gone)
                    void openList();
            }).catch(fail);
        },

        onAddField(change) {
            state.patch({ ...change, message: "" });
        },

        onTrust() {
            const path = state.get().data.path.trim();
            if (!path) {
                state.patch({ message: "Enter a certificate path." });
                return;
            }
            const passphrase = state.get().data.passphrase;
            state.patch({ busy: true, message: "" });
            void api.add(path, passphrase || undefined).then(() => {
                if (!gone)
                    void openList();
            }).catch(fail);
        },

        onCancelAdd() {
            void openList();
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
