// The VPN card's state machine, against a fake com.palm.vpn.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createFakeLuna, type FakeLuna } from "@webos/api/infra/luna/fake.service.ts";
import { createVpnService, stateLabel } from "../src/vpn.service.ts";
import type { Payload } from "@webos/api/infra/luna/service.ts";

const VPN = "luna://com.palm.vpn/";
const LIST = `${VPN}getProfileList`;
const AGENTS = `${VPN}getAgents`;
const DETAILS = `${VPN}getProfileDetails`;
const ADD = `${VPN}addProfile`;
const CONNECT = `${VPN}connect`;
const DISCONNECT = `${VPN}disconnect`;
const DELETE = `${VPN}deleteProfile`;

const settle = () => new Promise((resolve) => setImmediate(resolve));

const profiles = () => ({
    returnValue: true,
    subscribed: true,
    vpnProfiles: [
        {
            vpnProfileName: "Work",
            vpnProfileConnectState: "disconnected",
            vpnAgentGuid: "com.gachlab.openvpn",
        },
    ],
});

const setup = () => {
    const luna = createFakeLuna();
    luna.answer(AGENTS, () => ({
        returnValue: true,
        vpnAgents: [
            { vpnAgentGuid: "com.gachlab.openvpn", vpnAgentLabel: "OpenVPN", vpnAgentTechnology: ["ssl"] },
            { vpnAgentGuid: "com.gachlab.wireguard", vpnAgentLabel: "WireGuard", vpnAgentTechnology: ["wireguard"] },
        ],
    }));
    luna.answer(LIST, () => profiles());
    const service = createVpnService(luna);
    return { luna, service, data: () => service.getState().data };
};

const push = (luna: FakeLuna, reply: Payload) => {
    const status = luna.subscribers.find((one) => one.uri === LIST);
    assert.ok(status, "the card is watching com.palm.vpn");
    status.push({ returnValue: true, ...reply });
};

const payloads = (luna: FakeLuna, uri: string): Payload[] =>
    luna.calls.filter((call) => call.uri === uri).map((call) => call.payload);

describe("state labels", () => {
    test("HP's connect states are uppercased for the list", () => {
        assert.equal(stateLabel("connected"), "CONNECTED");
        assert.equal(stateLabel("connecting"), "CONNECTING");
        assert.equal(stateLabel("disconnected"), "");
    });
});

describe("the profile list", () => {
    test("subscribes and paints what the service pushes", async () => {
        const { luna, service, data } = setup();
        service.onShown();
        await settle();
        push(luna, profiles());
        await settle();
        assert.equal(data().screen, "list");
        assert.equal(data().profiles.length, 1);
        assert.equal(data().profiles[0]?.name, "Work");
        assert.equal(data().agents.length, 2);
    });

    test("opening add puts an empty OpenVPN form on screen", async () => {
        const { service, data } = setup();
        service.onShown();
        await settle();
        service.onOpenAdd();
        assert.equal(data().screen, "add");
        assert.equal(data().add?.agentGuid, "com.gachlab.openvpn");
        assert.equal(data().add?.name, "");
    });
});

describe("adding a profile", () => {
    test("refuses a blank name or server without calling the bus", async () => {
        const { luna, service, data } = setup();
        service.onShown();
        await settle();
        service.onOpenAdd();
        service.onSaveAdd();
        assert.match(data().message, /required/i);
        assert.equal(payloads(luna, ADD).length, 0);
    });

    test("saves an OpenVPN profile and returns to the list", async () => {
        const { luna, service, data } = setup();
        luna.answer(ADD, () => ({ returnValue: true }));
        service.onShown();
        await settle();
        service.onOpenAdd();
        service.onAddField({ name: "Home", remote: "vpn.example.com", userName: "ana", password: "x" });
        service.onSaveAdd();
        await settle();
        assert.equal(data().screen, "list");
        const sent = payloads(luna, ADD);
        assert.equal(sent.length, 1);
        assert.equal(sent[0]?.vpnProfileName, "Home");
        assert.equal((sent[0]?.vpnProfile as Payload).remote, "vpn.example.com");
    });
});

describe("connection details", () => {
    test("connect and disconnect call the service with the profile name", async () => {
        const { luna, service, data } = setup();
        luna.answer(DETAILS, () => ({
            returnValue: true,
            vpnProfileName: "Work",
            vpnAgentGuid: "com.gachlab.openvpn",
            vpnProfileConnectState: "disconnected",
            vpnProfile: { remote: "vpn.example.com", userName: "ana" },
        }));
        luna.answer(CONNECT, () => ({ returnValue: true }));
        luna.answer(DISCONNECT, () => ({ returnValue: true }));
        luna.answer(DELETE, () => ({ returnValue: true }));
        service.onShown();
        await settle();
        service.onOpenDetails("Work");
        await settle();
        assert.equal(data().screen, "details");
        assert.equal(data().details?.remote, "vpn.example.com");

        service.onConnectDisconnect();
        await settle();
        assert.equal(payloads(luna, CONNECT)[0]?.vpnProfileName, "Work");

        // Pretend the subscription said it connected.
        push(luna, {
            vpnProfiles: [{
                vpnProfileName: "Work",
                vpnProfileConnectState: "connected",
                vpnAgentGuid: "com.gachlab.openvpn",
            }],
        });
        await settle();
        assert.equal(data().details?.connectState, "connected");

        service.onConnectDisconnect();
        await settle();
        assert.equal(payloads(luna, DISCONNECT)[0]?.vpnProfileName, "Work");

        service.onDelete();
        await settle();
        assert.equal(payloads(luna, DELETE)[0]?.vpnProfileName, "Work");
        assert.equal(data().screen, "list");
    });
});
