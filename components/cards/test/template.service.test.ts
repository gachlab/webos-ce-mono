// The template card's service: the state machine every other card copies.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createFakeLuna } from "#lib/infra/luna/fake.service.ts";
import { createTemplateService, factsOf } from "#lib/services/template.service.ts";
import type { State } from "#lib/helpers/create-state.ts";
import type { TemplateData } from "#lib/services/template.service.ts";

const PROFILE = "luna://com.palm.deviceprofile/getDeviceProfile";

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
        assert.deepEqual(luna.calls.map((c) => c.uri), [PROFILE]);
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
        assert.equal(luna.calls.length, 2);
    });

    test("a second ask while the first is in flight is not a second call", async () => {
        const { luna, service } = setup();
        luna.silence(PROFILE);
        service.onShown();
        service.onRetry();
        await settle();
        assert.equal(luna.calls.length, 1);
    });

    test("missing fields are empty, never the word undefined on screen", () => {
        assert.deepEqual(factsOf({}), { model: "webOS device", version: "", serial: "" });
        assert.deepEqual(factsOf({ deviceInfo: { deviceModel: "", softwareVersion: 3 } }),
                         { model: "webOS device", version: "", serial: "" });
    });
});
