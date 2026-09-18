// The Screen & Lock card's state machine, against a fake bus.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createFakeLuna, type FakeLuna } from "@webos/api/infra/luna/fake.service.ts";
import { createScreenLockService, timeoutLabel } from "../src/screenlock.service.ts";
import type { Payload } from "@webos/api/infra/luna/service.ts";

const DISPLAY = "luna://com.palm.display/control/";
const SYSTEM = "luna://com.palm.systemservice/";
const MANAGER = "luna://com.palm.systemmanager/";

const settle = () => new Promise((resolve) => setImmediate(resolve));

const payloads = (luna: FakeLuna, uri: string): Payload[] =>
    luna.calls.filter((call) => call.uri === uri).map((call) => call.payload);

const setup = () => {
    const luna = createFakeLuna();
    luna.answer(`${DISPLAY}getProperty`, () => ({
        returnValue: true,
        timeout: 120,
        maximumBrightness: 40,
    }));
    luna.answer(`${DISPLAY}setProperty`, () => ({ returnValue: true }));
    luna.answer(`${SYSTEM}getPreferences`, () => ({
        returnValue: true,
        enableALS: true,
        sysUiEnableNextPrevGestures: false,
        showAlertsWhenLocked: true,
        BlinkNotifications: false,
        lockTimeout: 60,
    }));
    luna.answer(`${SYSTEM}setPreferences`, () => ({ returnValue: true }));
    luna.answer(`${MANAGER}getDeviceLockMode`, () => ({
        returnValue: true,
        lockMode: "none",
    }));
    luna.answer(`${MANAGER}setDevicePasscode`, () => ({ returnValue: true }));
    const service = createScreenLockService(luna);
    return { luna, service, data: () => service.getState().data };
};

describe("timeout labels", () => {
    test("maps HP's display timeout seconds to captions", () => {
        assert.equal(timeoutLabel(60), "1 minute");
        assert.equal(timeoutLabel(120), "2 minutes");
        assert.equal(timeoutLabel(300), "5 minutes");
        assert.equal(timeoutLabel(600), "10 minutes");
        // Mutation guard: a wrong case for 120 must not pretend to be 1 minute.
        assert.notEqual(timeoutLabel(120), "1 minute");
        assert.notEqual(timeoutLabel(120), "5 minutes");
    });
});

describe("loading preferences", () => {
    test("reads display properties and system prefs on shown", async () => {
        const { service, data } = setup();
        service.onShown();
        await settle();
        assert.equal(data().timeout, 120);
        assert.equal(data().brightness, 40);
        assert.equal(data().gestures, false);
        assert.equal(data().showWhenLocked, true);
        assert.equal(data().lockTimeout, 60);
    });
});

describe("display controls", () => {
    test("Turn off After writes timeout to setProperty", async () => {
        const { luna, service } = setup();
        service.onShown();
        await settle();
        service.onTimeout(300);
        await settle();
        assert.equal(payloads(luna, `${DISPLAY}setProperty`)[0]?.timeout, 300);
    });

    test("Auto Dim writes enableALS", async () => {
        const { luna, service, data } = setup();
        service.onShown();
        await settle();
        service.onAutoDim(false);
        await settle();
        assert.equal(data().enableALS, false);
        assert.equal(payloads(luna, `${SYSTEM}setPreferences`)[0]?.enableALS, false);
    });
});

describe("secure unlock", () => {
    test("Off clears the passcode; PIN opens configure then saves", async () => {
        const { luna, service, data } = setup();
        service.onShown();
        await settle();
        service.onLockMode("none");
        await settle();
        assert.equal(payloads(luna, `${MANAGER}setDevicePasscode`)[0]?.lockMode, "none");

        service.onLockMode("pin");
        assert.equal(data().screen, "configure");
        service.onConfigureField({ passcode: "1234", passcodeConfirm: "1234" });
        service.onSaveConfigure();
        await settle();
        assert.equal(data().screen, "main");
        assert.equal(data().lockMode, "pin");
        const saved = payloads(luna, `${MANAGER}setDevicePasscode`).at(-1);
        assert.equal(saved?.lockMode, "pin");
        assert.equal(saved?.passCode, "1234");
    });
});
