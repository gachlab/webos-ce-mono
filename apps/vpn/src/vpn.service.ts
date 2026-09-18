// The VPN settings card: list, add, and connection details.
//
// Screens and wording follow HP's VPN card on the TouchPad CE image (spec, not
// code). The service is com.palm.vpn from nm-connectionmanager.

import { createState, type State, type StateHolder, type Unsubscribe } from "@webos/api/helpers/create-state.ts";
import { createNavigation } from "@webos/api/services/navigation.service.ts";
import {
    createVpn, type AgentGuid, type ConnectState, type ProfileFields, type VpnAgent,
    type VpnProfile,
} from "./luna/vpn.ts";
import { LunaCallError, errorTextOf, type LunaService, type Subscription } from "@webos/api/infra/luna/service.ts";
import type { LaunchParams } from "@webos/api/infra/app/service.ts";

export type VpnScreen = "list" | "add" | "configure" | "details";

export interface VpnData {
    readonly screen: VpnScreen;
    readonly profiles: VpnProfile[];
    readonly agents: VpnAgent[];
    readonly caption: string;
    readonly busy: boolean;
    readonly message: string;
    readonly swipeOpen?: string | undefined;
    readonly choosing?: boolean;
    readonly add?: ProfileFields;
    readonly details?: VpnProfile;
}

export interface VpnService {
    getState(): State<VpnData>;
    onStateChange(listener: (state: State<VpnData>) => void): Unsubscribe;
    onShown(params?: LaunchParams): void;
    onHidden(): void;
    onBack(): boolean;
    dispose(): void;

    onOpenAdd(): void;
    onOpenDetails(name: string): void;
    onToggleConnect(name: string): void;
    onSwipe(name: string, open: boolean): void;
    onDeleteProfile(name: string): void;
    onAddField(change: Partial<ProfileFields>): void;
    onChooseAgent(open: boolean): void;
    onCancelAdd(): void;
    onNextAdd(): void;
    onSaveAdd(): void;
    onConnectDisconnect(): void;
    onDelete(): void;
}

const emptyFields = (agentGuid: AgentGuid = "com.gachlab.openvpn"): ProfileFields => ({
    name: "",
    agentGuid,
    remote: "",
    userName: "",
    password: "",
    privateKey: "",
    peerPublicKey: "",
    address: "",
});

// HP shows the uppercase line under the name only while the tunnel is moving.
// Connected uses the checkmark instead; disconnected shows nothing.
export const progressLabel = (state: ConnectState): string => {
    switch (state) {
    case "connecting": return "CONNECTING";
    case "disconnecting": return "DISCONNECTING";
    case "reconnecting": return "RECONNECTING";
    default: return "";
    }
};

export const stateLabel = (state: ConnectState): string => {
    switch (state) {
    case "connected": return "CONNECTED";
    case "connecting": return "CONNECTING";
    case "disconnecting": return "DISCONNECTING";
    case "reconnecting": return "RECONNECTING";
    case "connectfailed": return "FAILED";
    default: return "DISCONNECTED";
    }
};

export const isBusyState = (state: ConnectState): boolean =>
    state === "connecting" || state === "disconnecting" || state === "reconnecting";

export const isActiveState = (state: ConnectState): boolean =>
    state === "connected" || isBusyState(state);

const profileNamed = (profiles: VpnProfile[], name: string): VpnProfile | undefined =>
    profiles.find((p) => p.name === name);

export const createVpnService = (luna: LunaService): VpnService => {
    const vpn = createVpn(luna);
    const screens = createNavigation<VpnScreen>("list");
    const state: StateHolder<VpnData> = createState<VpnData>({
        name: "vpn:list",
        data: {
            screen: "list",
            profiles: [],
            agents: [],
            caption: "",
            busy: false,
            message: "",
        },
    });
    let gone = false;
    let subscription: Subscription | undefined;

    const show = (screen: VpnScreen) => {
        if (screens.now() !== screen)
            screens.open(screen);
        state.patch({ screen }, `vpn:${screen}`);
    };

    screens.onChange(() => state.patch({ screen: screens.now() }, `vpn:${screens.now()}`));

    const fail = (error: unknown) => {
        if (gone)
            return;
        const message = error instanceof LunaCallError ? errorTextOf(error.reply)
            : error instanceof Error ? error.message : String(error);
        state.patch({ busy: false, message });
    };

    const openList = () => {
        while (screens.back())
            ;
        const current = state.get().data;
        state.set({
            name: "vpn:list",
            data: {
                screen: "list",
                profiles: current.profiles,
                agents: current.agents,
                caption: current.profiles.length === 0 ? "No VPN profiles" : "",
                busy: false,
                message: "",
            },
        });
    };

    const deleteNamed = (name: string) => {
        const profile = profileNamed(state.get().data.profiles, name)
            ?? state.get().data.details;
        if (!profile || state.get().data.busy)
            return;
        state.patch({ busy: true, message: "", swipeOpen: undefined });
        const run = async () => {
            // HP disconnects first when the tunnel is up or still coming up.
            if (profile.connectState === "connected" || profile.connectState === "connecting"
                || profile.connectState === "reconnecting") {
                await vpn.disconnect(profile.name, profile.agentGuid);
            }
            await vpn.deleteProfile(profile.name);
            if (!gone)
                openList();
        };
        void run().catch(fail);
    };

    return {
        getState: () => state.get(),
        onStateChange: (listener) => state.subscribe(listener),

        onShown() {
            gone = false;
            if (subscription)
                return;
            subscription = vpn.watchProfiles((profiles) => {
                if (gone)
                    return;
                const details = state.get().data.details;
                const updated = details
                    ? profiles.find((p) => p.name === details.name)
                    : undefined;
                state.patch({
                    profiles,
                    caption: profiles.length === 0 ? "No VPN profiles" : "",
                    ...(updated ? { details: { ...details!, connectState: updated.connectState } } : {}),
                });
            });
            void vpn.getAgents().then((agents) => {
                if (!gone)
                    state.patch({ agents });
            }).catch(fail);
        },

        onHidden() {
            subscription?.cancel();
            subscription = undefined;
        },

        onBack() {
            if (screens.now() === "list")
                return false;
            if (!screens.back())
                return false;
            const screen = screens.now();
            const current = state.get().data;
            if (screen === "list") {
                openList();
                return true;
            }
            state.set({
                name: `vpn:${screen}`,
                data: {
                    screen,
                    profiles: current.profiles,
                    agents: current.agents,
                    caption: current.caption,
                    busy: false,
                    message: "",
                    ...(screen === "details" && current.details ? { details: current.details } : {}),
                    ...((screen === "add" || screen === "configure") && current.add
                        ? { add: current.add } : {}),
                },
            });
            return true;
        },

        dispose() {
            gone = true;
            subscription?.cancel();
            subscription = undefined;
            state.clear();
        },

        onOpenAdd() {
            const agents = state.get().data.agents;
            state.patch({
                add: emptyFields(agents[0]?.guid ?? "com.gachlab.openvpn"),
                message: "",
            });
            show("add");
        },

        onOpenDetails(name) {
            state.patch({ busy: true, message: "", swipeOpen: undefined });
            void vpn.getProfileDetails(name).then((details) => {
                if (gone)
                    return;
                state.patch({ details, busy: false });
                show("details");
            }).catch(fail);
        },

        onToggleConnect(name) {
            const profile = profileNamed(state.get().data.profiles, name);
            if (!profile || state.get().data.busy || isBusyState(profile.connectState))
                return;
            state.patch({ busy: true, message: "", swipeOpen: undefined });
            const op = profile.connectState === "connected"
                ? vpn.disconnect(profile.name, profile.agentGuid)
                : vpn.connect(profile.name, profile.agentGuid);
            void op.then(() => {
                if (!gone)
                    state.patch({ busy: false });
            }).catch(fail);
        },

        onSwipe(name, open) {
            const profile = profileNamed(state.get().data.profiles, name);
            if (profile && isBusyState(profile.connectState))
                return;
            state.patch({ swipeOpen: open ? name : undefined });
        },

        onDeleteProfile(name) {
            deleteNamed(name);
        },

        onAddField(change) {
            const add = state.get().data.add;
            if (!add)
                return;
            state.patch({ add: { ...add, ...change }, message: "", choosing: false });
        },

        onChooseAgent(open) {
            state.patch({ choosing: open });
        },

        onCancelAdd() {
            openList();
        },

        // HP's Next: leave the host/type step for the configure form. Profile
        // name defaults to the server, as ConfigureProfileView does.
        onNextAdd() {
            const add = state.get().data.add;
            if (!add)
                return;
            if (!add.remote.trim()) {
                state.patch({ message: "Enter a VPN server." });
                return;
            }
            const name = add.name.trim() || add.remote.trim();
            state.patch({ add: { ...add, name }, message: "" });
            show("configure");
        },

        onSaveAdd() {
            const add = state.get().data.add;
            if (!add)
                return;
            if (!add.name.trim() || !add.remote.trim()) {
                state.patch({ message: "Profile name and server are required." });
                return;
            }
            state.patch({ busy: true, message: "" });
            void vpn.addProfile(add).then(async () => {
                await vpn.connect(add.name, add.agentGuid);
                if (!gone)
                    openList();
            }).catch(fail);
        },

        onConnectDisconnect() {
            const details = state.get().data.details;
            if (!details || state.get().data.busy)
                return;
            const connecting = details.connectState === "disconnected"
                || details.connectState === "connectfailed";
            state.patch({ busy: true, message: "" });
            const op = connecting
                ? vpn.connect(details.name, details.agentGuid)
                : vpn.disconnect(details.name, details.agentGuid);
            void op.then(() => {
                if (!gone)
                    state.patch({ busy: false });
            }).catch(fail);
        },

        onDelete() {
            const details = state.get().data.details;
            if (!details)
                return;
            deleteNamed(details.name);
        },
    };
};
