// The Bluetooth card's state machine, against a fake com.palm.btmonitor and
// com.palm.bluetooth.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createFakeLuna, type FakeLuna } from "@webos/api/infra/luna/fake.service.ts";
import { createBtService, radioOn, radioBusy, statusLabel } from "../src/bluetooth.service.ts";
import type { Payload } from "@webos/api/infra/luna/service.ts";

const MONITOR = "luna://com.palm.btmonitor/monitor/";
const GAP = "luna://com.palm.bluetooth/gap/";
const PROF = "luna://com.palm.bluetooth/prof/";
const RADIO_SUB = `${MONITOR}subscribenotifications`;
const GAP_SUB = `${GAP}subscribenotifications`;

const settle = () => new Promise((resolve) => setImmediate(resolve));

const headset = (status = "disconnected", paired = true) => ({
    address: "F8:DF:15:F2:29:ED",
    name: "AKG Y500 WIRELESS",
    status,
    cod: 2360324,
    paired,
    battery: 60,
    icon: "audio-headset",
});

const setup = () => {
    const luna = createFakeLuna();
    luna.answer(RADIO_SUB, () => ({ returnValue: true, subscribed: true, radio: "on" }));
    luna.answer(GAP_SUB, () => ({ returnValue: true, subscribed: true, trusteddevices: [] }));
    const service = createBtService(luna);
    return { luna, service, data: () => service.getState().data };
};

// Push onto a named subscription. The radio and the gap channels are separate
// URIs; devices and pairing share the gap URI, so a device push and a prompt
// push are told apart by their payload, not their channel.
const pushTo = (luna: FakeLuna, uri: string, reply: Payload) => {
    const subs = luna.subscribers.filter((one) => one.uri === uri);
    assert.ok(subs.length > 0, `the card is watching ${uri}`);
    for (const sub of subs)
        sub.push({ returnValue: true, ...reply });
};

const payloads = (luna: FakeLuna, uri: string): Payload[] =>
    luna.calls.filter((call) => call.uri === uri).map((call) => call.payload);

describe("radio helpers", () => {
    test("on and turning-on read as on; transitions read as busy", () => {
        assert.equal(radioOn("on"), true);
        assert.equal(radioOn("turningon"), true);
        assert.equal(radioOn("off"), false);
        assert.equal(radioBusy("turningon"), true);
        assert.equal(radioBusy("turningoff"), true);
        assert.equal(radioBusy("on"), false);
    });

    test("the moving-status line only shows while connecting or disconnecting", () => {
        assert.equal(statusLabel("connecting"), "CONNECTING...");
        assert.equal(statusLabel("disconnecting"), "DISCONNECTING...");
        assert.equal(statusLabel("connected"), "");
        assert.equal(statusLabel("disconnected"), "");
    });
});

describe("the device list", () => {
    test("subscribes to the radio and the devices, and paints both", async () => {
        const { luna, service, data } = setup();
        service.onShown();
        await settle();
        pushTo(luna, RADIO_SUB, { radio: "on" });
        pushTo(luna, GAP_SUB, { trusteddevices: [headset()] });
        await settle();
        assert.equal(data().radio, "on");
        assert.equal(data().devices.length, 1);
        assert.equal(data().devices[0]?.name, "AKG Y500 WIRELESS");
        assert.equal(data().devices[0]?.battery, 60);
    });

    test("the radio toggle calls radioon when off and radiooff when on", async () => {
        const { luna, service } = setup();
        luna.answer(`${MONITOR}radioon`, () => ({ returnValue: true }));
        luna.answer(`${MONITOR}radiooff`, () => ({ returnValue: true }));
        service.onShown();
        await settle();
        pushTo(luna, RADIO_SUB, { radio: "off" });
        await settle();
        service.onToggleRadio();
        await settle();
        assert.equal(payloads(luna, `${MONITOR}radioon`).length, 1);

        pushTo(luna, RADIO_SUB, { radio: "on" });
        await settle();
        service.onToggleRadio();
        await settle();
        assert.equal(payloads(luna, `${MONITOR}radiooff`).length, 1);
    });

    test("tapping an unpaired device pairs it", async () => {
        const { luna, service } = setup();
        luna.answer(`${GAP}pair`, () => ({ returnValue: true }));
        service.onShown();
        await settle();
        pushTo(luna, GAP_SUB, { trusteddevices: [headset("disconnected", false)] });
        await settle();
        service.onTapDevice("F8:DF:15:F2:29:ED");
        await settle();
        assert.equal(payloads(luna, `${GAP}pair`)[0]?.address, "F8:DF:15:F2:29:ED");
    });

    test("tapping a paired, disconnected device connects it", async () => {
        const { luna, service } = setup();
        luna.answer(`${PROF}profconnect`, () => ({ returnValue: true }));
        service.onShown();
        await settle();
        pushTo(luna, GAP_SUB, { trusteddevices: [headset("disconnected", true)] });
        await settle();
        service.onTapDevice("F8:DF:15:F2:29:ED");
        await settle();
        const sent = payloads(luna, `${PROF}profconnect`)[0];
        assert.equal(sent?.address, "F8:DF:15:F2:29:ED");
        assert.equal(sent?.profile, "all");
    });

    test("tapping a connected device disconnects it", async () => {
        const { luna, service } = setup();
        luna.answer(`${PROF}profdisconnect`, () => ({ returnValue: true }));
        service.onShown();
        await settle();
        pushTo(luna, GAP_SUB, { trusteddevices: [headset("connected", true)] });
        await settle();
        service.onTapDevice("F8:DF:15:F2:29:ED");
        await settle();
        assert.equal(payloads(luna, `${PROF}profdisconnect`)[0]?.address, "F8:DF:15:F2:29:ED");
    });

    test("swipe forget removes the device", async () => {
        const { luna, service } = setup();
        luna.answer(`${GAP}removedevice`, () => ({ returnValue: true }));
        service.onShown();
        await settle();
        pushTo(luna, GAP_SUB, { trusteddevices: [headset("disconnected", true)] });
        await settle();
        service.onForget("F8:DF:15:F2:29:ED");
        await settle();
        assert.equal(payloads(luna, `${GAP}removedevice`)[0]?.address, "F8:DF:15:F2:29:ED");
    });

    test("discovery toggles start and stop, but only when the radio is on", async () => {
        const { luna, service, data } = setup();
        luna.answer(`${GAP}startdiscovery`, () => ({ returnValue: true }));
        luna.answer(`${GAP}stopdiscovery`, () => ({ returnValue: true }));
        service.onShown();
        await settle();
        // radio off: a discovery toggle does nothing.
        pushTo(luna, RADIO_SUB, { radio: "off" });
        await settle();
        service.onToggleDiscovery();
        await settle();
        assert.equal(payloads(luna, `${GAP}startdiscovery`).length, 0);
        // radio on: it starts, then stops.
        pushTo(luna, RADIO_SUB, { radio: "on" });
        await settle();
        service.onToggleDiscovery();
        await settle();
        assert.equal(payloads(luna, `${GAP}startdiscovery`).length, 1);
        assert.equal(data().discovering, true);
        service.onToggleDiscovery();
        await settle();
        assert.equal(payloads(luna, `${GAP}stopdiscovery`).length, 1);
    });
});

describe("pairing prompts", () => {
    test("a numeric-comparison prompt shows the passkey and confirms", async () => {
        const { luna, service, data } = setup();
        luna.answer(`${GAP}supplyconfirmation`, () => ({ returnValue: true }));
        service.onShown();
        await settle();
        pushTo(luna, GAP_SUB, {
            notification: "notifnpairingrequest",
            prompt: "requestconfirmation",
            address: "F8:DF:15:F2:29:ED",
            name: "AKG",
            passkey: "012345",
        });
        await settle();
        assert.equal(data().screen, "pairing");
        assert.equal(data().prompt?.kind, "requestconfirmation");
        assert.equal(data().prompt?.passkey, "012345");

        service.onPromptAccept();
        await settle();
        assert.equal(payloads(luna, `${GAP}supplyconfirmation`)[0]?.accept, true);
        assert.equal(data().prompt, undefined);
        assert.equal(data().screen, "list");
    });

    test("a passkey prompt sends what the user typed", async () => {
        const { luna, service } = setup();
        luna.answer(`${GAP}supplypasskey`, () => ({ returnValue: true }));
        service.onShown();
        await settle();
        pushTo(luna, GAP_SUB, {
            notification: "notifnpairingrequest",
            prompt: "requestpasskey",
            address: "AA:BB:CC:DD:EE:FF",
            name: "Keyboard",
        });
        await settle();
        service.onPromptEntry("123456");
        service.onPromptAccept();
        await settle();
        assert.equal(payloads(luna, `${GAP}supplypasskey`)[0]?.passkey, "123456");
    });

    test("rejecting a confirmation sends accept:false", async () => {
        const { luna, service, data } = setup();
        luna.answer(`${GAP}supplyconfirmation`, () => ({ returnValue: true }));
        service.onShown();
        await settle();
        pushTo(luna, GAP_SUB, {
            notification: "notifnpairingrequest",
            prompt: "requestconfirmation",
            address: "F8:DF:15:F2:29:ED",
            name: "AKG",
            passkey: "012345",
        });
        await settle();
        service.onPromptReject();
        await settle();
        assert.equal(payloads(luna, `${GAP}supplyconfirmation`)[0]?.accept, false);
        assert.equal(data().prompt, undefined);
    });
});

describe("connection details", () => {
    test("opening details, connecting, and forgetting", async () => {
        const { luna, service, data } = setup();
        luna.answer(`${PROF}profconnect`, () => ({ returnValue: true }));
        luna.answer(`${GAP}removedevice`, () => ({ returnValue: true }));
        service.onShown();
        await settle();
        pushTo(luna, GAP_SUB, { trusteddevices: [headset("disconnected", true)] });
        await settle();
        service.onOpenDetails("F8:DF:15:F2:29:ED");
        assert.equal(data().screen, "details");
        assert.equal(data().details?.name, "AKG Y500 WIRELESS");

        service.onConnectDisconnect();
        await settle();
        assert.equal(payloads(luna, `${PROF}profconnect`)[0]?.address, "F8:DF:15:F2:29:ED");

        service.onForgetDetails();
        await settle();
        assert.equal(payloads(luna, `${GAP}removedevice`)[0]?.address, "F8:DF:15:F2:29:ED");
        assert.equal(data().screen, "list");
    });
});
