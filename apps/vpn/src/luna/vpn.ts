// com.palm.vpn, typed.
//
// Vocabulary matches what HP's card and the system menu drawer expect:
// vpnProfiles, vpnProfileName, vpnProfileConnectState, vpnAgentGuid. The service
// behind it is ours (nm-connectionmanager on NetworkManager).

import type { LunaService, Payload, Subscription } from "@webos/api/infra/luna/service.ts";

export type ConnectState =
    | "connected" | "connecting" | "disconnected" | "disconnecting"
    | "reconnecting" | "connectfailed";

export type AgentGuid = "com.gachlab.openvpn" | "com.gachlab.wireguard";

export interface VpnProfile {
    readonly name: string;
    readonly connectState: ConnectState;
    readonly agentGuid: AgentGuid;
    readonly remote?: string;
    readonly userName?: string;
}

export interface VpnAgent {
    readonly guid: AgentGuid;
    readonly label: string;
    readonly technology: string;
}

export interface ProfileFields {
    readonly name: string;
    readonly agentGuid: AgentGuid;
    readonly remote: string;
    readonly userName: string;
    readonly password: string;
    readonly privateKey: string;
    readonly peerPublicKey: string;
    readonly address: string;
}

const text = (value: unknown): string => (typeof value === "string" ? value : "");

const connectStateOf = (value: unknown): ConnectState => {
    const name = text(value);
    if (name === "connected" || name === "connecting" || name === "disconnecting"
        || name === "reconnecting" || name === "connectfailed")
        return name;
    return "disconnected";
};

const agentOf = (value: unknown): AgentGuid =>
    text(value) === "com.gachlab.wireguard" ? "com.gachlab.wireguard" : "com.gachlab.openvpn";

const profileOf = (raw: Payload): VpnProfile => {
    const remote = text((raw.vpnProfile as Payload | undefined)?.remote) || text(raw.remote);
    const userName = text((raw.vpnProfile as Payload | undefined)?.userName) || text(raw.userName);
    return {
        name: text(raw.vpnProfileName),
        connectState: connectStateOf(raw.vpnProfileConnectState),
        agentGuid: agentOf(raw.vpnAgentGuid),
        ...(remote ? { remote } : {}),
        ...(userName ? { userName } : {}),
    };
};

const agentFrom = (raw: Payload): VpnAgent => ({
    guid: agentOf(raw.vpnAgentGuid),
    label: text(raw.vpnAgentLabel) || text(raw.vpnAgentGuid),
    technology: Array.isArray(raw.vpnAgentTechnology)
        ? text(raw.vpnAgentTechnology[0])
        : text(raw.vpnAgentTechnology),
});

export interface VpnClient {
    watchProfiles(onList: (profiles: VpnProfile[]) => void): Subscription;
    getAgents(): Promise<VpnAgent[]>;
    getProfileDetails(name: string): Promise<VpnProfile>;
    addProfile(fields: ProfileFields): Promise<void>;
    updateProfile(fields: ProfileFields): Promise<void>;
    deleteProfile(name: string): Promise<void>;
    connect(name: string, agentGuid: AgentGuid): Promise<void>;
    disconnect(name: string, agentGuid: AgentGuid): Promise<void>;
}

export const createVpn = (luna: LunaService): VpnClient => {
    const base = "luna://com.palm.vpn/";

    const bodyOf = (fields: ProfileFields): Payload => ({
        vpnProfileName: fields.name,
        vpnAgentGuid: fields.agentGuid,
        vpnProfile: {
            remote: fields.remote,
            userName: fields.userName,
            password: fields.password,
            privateKey: fields.privateKey,
            peerPublicKey: fields.peerPublicKey,
            address: fields.address,
        },
    });

    return {
        watchProfiles(onList) {
            return luna.subscribe(`${base}getProfileList`, {}, (payload) => {
                const list = Array.isArray(payload.vpnProfiles) ? payload.vpnProfiles : [];
                onList(list.map((item) => profileOf(item as Payload)));
            });
        },

        async getAgents() {
            const reply = await luna.call(`${base}getAgents`, {});
            const list = Array.isArray(reply.vpnAgents) ? reply.vpnAgents : [];
            return list.map((item) => agentFrom(item as Payload));
        },

        async getProfileDetails(name) {
            const reply = await luna.call(`${base}getProfileDetails`, { vpnProfileName: name });
            return profileOf(reply);
        },

        async addProfile(fields) {
            await luna.call(`${base}addProfile`, bodyOf(fields));
        },

        async updateProfile(fields) {
            await luna.call(`${base}updateProfile`, bodyOf(fields));
        },

        async deleteProfile(name) {
            await luna.call(`${base}deleteProfile`, { vpnProfileName: name });
        },

        async connect(name, agentGuid) {
            await luna.call(`${base}connect`, {
                vpnProfileName: name,
                vpnAgentGuid: agentGuid,
                vpnProfileConnectState: "disconnected",
            });
        },

        async disconnect(name, agentGuid) {
            await luna.call(`${base}disconnect`, {
                vpnProfileName: name,
                vpnAgentGuid: agentGuid,
                vpnProfileConnectState: "connected",
            });
        },
    };
};
