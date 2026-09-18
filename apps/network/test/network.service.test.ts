// The Networking card's state machine, against a fake connectionmanager.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createFakeLuna, type FakeLuna } from "@webos/api/infra/luna/fake.service.ts";
import { CAPTIVE_PROBE_URL, createNetworkService } from "../src/network.service.ts";
import type { Payload } from "@webos/api/infra/luna/service.ts";

const CM = "luna://com.palm.connectionmanager/";
const STATUS = `${CM}getstatus`;
const GET_PROXY = `${CM}getNwProxiesConfig`;
const SET_PROXY = `${CM}configureNwProxies`;
const OPEN = "luna://com.palm.applicationManager/open";

const settle = () => new Promise((resolve) => setImmediate(resolve));

const status = (onInternet: string, online = false) => ({
    returnValue: true,
    subscribed: true,
    isInternetConnectionAvailable: online,
    wifi: { state: "connected", onInternet },
    wired: { state: "disconnected", onInternet: "no" },
});

const setup = () => {
    const luna = createFakeLuna();
    luna.answer(STATUS, () => status("no"));
    luna.answer(GET_PROXY, () => ({ returnValue: true, proxyInfoList: [] }));
    luna.answer(SET_PROXY, () => ({ returnValue: true }));
    luna.answer(OPEN, () => ({ returnValue: true }));
    const service = createNetworkService(luna);
    return { luna, service, data: () => service.getState().data };
};

const push = (luna: FakeLuna, reply: Payload) => {
    const sub = luna.subscribers.find((one) => one.uri === STATUS);
    assert.ok(sub, "the card is watching connectionmanager");
    sub.push({ returnValue: true, ...reply });
};

const payloads = (luna: FakeLuna, uri: string): Payload[] =>
    luna.calls.filter((call) => call.uri === uri).map((call) => call.payload);

describe("captive portal", () => {
    test("a captivePortal status opens Network Login", async () => {
        const { luna, service, data } = setup();
        service.onShown();
        await settle();
        push(luna, status("captivePortal"));
        await settle();
        assert.equal(data().screen, "portal");
        assert.match(data().note, /sign-in/i);
    });

    test("Open Login Page opens the captive probe URL", async () => {
        const { luna, service } = setup();
        service.onShown();
        await settle();
        push(luna, status("captivePortal"));
        await settle();
        service.onOpenLogin();
        await settle();
        assert.deepEqual(payloads(luna, OPEN), [{ target: CAPTIVE_PROBE_URL }]);
    });

    test("leaving captivePortal for online shows connected", async () => {
        const { luna, service, data } = setup();
        service.onShown();
        await settle();
        push(luna, status("captivePortal"));
        await settle();
        push(luna, status("yes", true));
        await settle();
        assert.equal(data().screen, "connected");
        assert.match(data().note, /connected/i);
    });
});

describe("proxy", () => {
    test("launch params open the proxy form for that scope", async () => {
        const { luna, service, data } = setup();
        luna.answer(GET_PROXY, () => ({
            returnValue: true,
            proxyInfoList: [{
                networkTechnology: "wifi",
                proxyScope: "12",
                proxyConfigType: "manualProxy",
                proxyServer: "proxy.example.com",
                proxyPort: 8080,
            }],
        }));
        service.onShown({ mode: "proxy", networkTechnology: "wifi", proxyScope: 12 });
        await settle();
        assert.equal(data().screen, "proxy");
        assert.equal(data().proxy?.type, "manualProxy");
        assert.equal(data().proxy?.proxyServer, "proxy.example.com");
        assert.equal(data().proxy?.proxyPort, "8080");
        assert.equal(data().proxy?.proxyScope, "12");
    });

    test("Save writes configureNwProxies for a manual proxy", async () => {
        const { luna, service, data } = setup();
        service.onShown({ mode: "proxy", networkTechnology: "wifi", proxyScope: "10" });
        await settle();
        service.onProxyField({ type: "manualProxy", proxyServer: "p.example", proxyPort: "3128" });
        service.onSaveProxy();
        await settle();
        assert.deepEqual(payloads(luna, SET_PROXY), [{
            action: "add",
            proxyInfo: {
                networkTechnology: "wifi",
                proxyScope: "10",
                proxyConfigType: "manualProxy",
                proxyServer: "p.example",
                proxyPort: 3128,
                isProxySecured: false,
            },
        }]);
        assert.equal(data().screen, "idle");
    });

    test("None removes the proxy for that scope", async () => {
        const { luna, service } = setup();
        service.onShown({ networkTechnology: "wifi", proxyScope: "10" });
        await settle();
        service.onProxyField({ type: "noProxy" });
        service.onSaveProxy();
        await settle();
        assert.deepEqual(payloads(luna, SET_PROXY), [{
            action: "rmv",
            proxyInfo: {
                networkTechnology: "wifi",
                proxyScope: "10",
                proxyConfigType: "noProxy",
            },
        }]);
    });

    test("a portal status does not steal the proxy screen", async () => {
        const { luna, service, data } = setup();
        service.onShown({ mode: "proxy", networkTechnology: "wifi", proxyScope: "1" });
        await settle();
        assert.equal(data().screen, "proxy");
        push(luna, status("captivePortal"));
        await settle();
        assert.equal(data().screen, "proxy");
    });
});
