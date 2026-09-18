// The template card's service: the state machine every other card copies.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createFakeLuna } from "@webos/api/infra/luna/fake.service.ts";
import { createTemplateService, factsOf } from "../src/template.service.ts";
import type { State } from "@webos/api/helpers/create-state.ts";
import type { TemplateData } from "../src/template.service.ts";

const PROFILE = "luna://com.palm.deviceprofile/getDeviceProfile";
const STATUS = "luna://com.palm.connectionmanager/getstatus";

const setup = () => {
    const luna = createFakeLuna();
    const logs: string[] = [];
    const service = createTemplateService({ luna, log: (message) => logs.push(message) });
    const states: State<TemplateData>[] = [];
    service.onStateChange((state) => states.push(state));
    return { luna, service, states, logs, names: () => states.map((s) => s.name) };
};

const settle = () => new Promise((resolve) => setImmediate(resolve));

describe("the template card", () => {
    test("asks the device who it is and shows it: loading, then ready", async () => {
        const { luna, service, states, names } = setup();
        luna.answer(PROFILE, () => ({
            returnValue: true,
            deviceInfo: { deviceModel: "ZBook", softwareVersion: "webOS-CE-3.0.5", nduId: "ab".repeat(20) },
        }));
        service.onShown();
        await settle();
        assert.deepEqual(names(), ["template:loading", "template:loading", "template:ready"]);
        assert.deepEqual(states.at(-1)!.data.device,
                         { model: "ZBook", version: "webOS-CE-3.0.5", serial: "ab".repeat(20) });
        assert.deepEqual(luna.calls.map((c) => c.uri), [STATUS, PROFILE]);
    });

    test("a service that says no leaves the card able to say so, and to try again", async () => {
        const { luna, service, states, logs, names } = setup();
        luna.answer(PROFILE, () => ({ returnValue: false, errorText: "com.palm.deviceprofile is not running" }));
        service.onShown();
        await settle();
        assert.equal(names().at(-1), "template:failed");
        assert.match(states.at(-1)!.error ?? "", /not running/);
        assert.equal(logs.length, 1);

        luna.answer(PROFILE, () => ({ returnValue: true, deviceInfo: { deviceModel: "ZBook" } }));
        service.onRetry();
        await settle();
        assert.equal(names().at(-1), "template:ready");
        assert.equal(states.at(-1)!.data.device?.model, "ZBook");
        assert.deepEqual(luna.calls.map((c) => c.uri), [STATUS, PROFILE, PROFILE]);
    });

    test("a second ask while the first is in flight is not a second call", async () => {
        const { luna, service } = setup();
        luna.silence(PROFILE);
        service.onShown();
        service.onRetry();
        await settle();
        assert.deepEqual(luna.calls.filter((c) => c.uri === PROFILE).length, 1);
    });

    test("a disposed card is not repainted by an answer that arrives late", async () => {
        const { luna, service, states } = setup();
        let answer = () => {};
        luna.answer(PROFILE, () => new Promise((resolve) => {
            answer = () => resolve({ returnValue: true, deviceInfo: { deviceModel: "Too late" } });
        }));
        service.onShown();
        await settle();
        const seen = states.length;
        service.dispose();
        answer();
        await settle();
        assert.equal(states.length, seen, "nothing was told about it");
        assert.equal(service.getState().name, "template:loading", "and the state it left behind is untouched");
        service.onRetry();
        await settle();
        assert.equal(luna.calls.filter((c) => c.uri === PROFILE).length, 1, "and it does not start another");
    });

    test("it follows the connection while it is being looked at, and not while it is not", async () => {
        const { luna, service, states } = setup();
        luna.answer(PROFILE, () => ({ returnValue: true, deviceInfo: {} }));
        service.onShown();
        await settle();
        assert.equal(luna.subscribers.length, 1, "one subscription, to the connection manager");
        assert.equal(luna.subscribers[0]!.uri, STATUS);

        luna.subscribers[0]!.push({
            returnValue: true, isInternetConnectionAvailable: true,
            wifi: { state: "connected", ssid: "home", ipAddress: "192.168.1.10" },
        });
        assert.deepEqual(states.at(-1)!.data.connection,
                         { online: true, through: "wifi", ssid: "home", ipAddress: "192.168.1.10" });

        service.onHidden();
        assert.deepEqual(luna.subscribers, [], "sent away, it stops listening");
        service.onShown();
        await settle();
        assert.equal(luna.subscribers.length, 1, "and starts again when it comes back");
        service.dispose();
        assert.deepEqual(luna.subscribers, []);
    });

    test("a row opens the second screen, and back comes out of it before the card closes", async () => {
        const { luna, service } = setup();
        luna.answer(PROFILE, () => ({ returnValue: true, deviceInfo: {} }));
        service.onShown();
        await settle();
        assert.equal(service.getState().data.screen, "device");
        service.onOpenNetwork();
        assert.equal(service.getState().data.screen, "network");
        assert.equal(service.onBack(), true, "back comes out of the second screen");
        assert.equal(service.getState().data.screen, "device");
        assert.equal(service.onBack(), false, "and then it is the card's turn to close");
    });

    test("missing fields are empty, never the word undefined on screen", () => {
        assert.deepEqual(factsOf({}), { model: "webOS device", version: "", serial: "" });
        assert.deepEqual(factsOf({ deviceInfo: { deviceModel: "", softwareVersion: 3 } }),
                         { model: "webOS device", version: "", serial: "" });
    });
});
