import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createFakeLuna } from "@webos/api/infra/luna/fake.service.ts";
import { confirmOf, createDeviceInfoService } from "../src/deviceinfo.service.ts";
import { eraseMethodOf, factsOf } from "../src/luna/deviceinfo.ts";

const settle = () => new Promise((resolve) => setImmediate(resolve));

describe("deviceinfo erase mapping", () => {
    test("maps each reset action to the HP power/storage method", () => {
        assert.equal(eraseMethodOf("reboot").method, "machineReboot");
        assert.equal(eraseMethodOf("shutdown").method, "machineOff");
        assert.equal(eraseMethodOf("eraseApps").method, "EraseVar");
        assert.equal(eraseMethodOf("eraseUsb").method, "EraseMedia");
        assert.equal(eraseMethodOf("eraseAll").method, "EraseAll");
        assert.equal(eraseMethodOf("wipe").method, "Wipe");
    });

    test("confirm dialogs keep HP titles", () => {
        assert.equal(confirmOf("wipe").title, "Secure Full Erase");
        assert.equal(confirmOf("eraseApps").confirm, "Erase Apps & Data");
    });
});

describe("deviceinfo service", () => {
    test("loads the device profile on show", async () => {
        const luna = createFakeLuna();
        luna.answer("luna://com.palm.deviceprofile/getDeviceProfile", () => ({
            returnValue: true,
            deviceInfo: {
                deviceModel: "TouchPad",
                softwareVersion: "3.0.5",
                nduId: "abc",
            },
        }));
        luna.answer("luna://com.palm.systemservice/getPreferences", () => ({
            returnValue: true,
            deviceName: "My Pad",
        }));
        const service = createDeviceInfoService(luna);
        service.onShown();
        await settle();
        const data = service.getState().data;
        assert.equal(data.deviceName, "My Pad");
        assert.equal(data.device?.model, "TouchPad");
        assert.deepEqual(factsOf({
            deviceInfo: { deviceModel: "X", softwareVersion: "1", nduId: "n" },
        }), { model: "X", version: "1", serial: "n" });
    });

    test("calls EraseVar when confirming erase apps", async () => {
        const luna = createFakeLuna();
        luna.answer("luna://com.palm.deviceprofile/getDeviceProfile", () => ({
            returnValue: true,
            deviceInfo: {},
        }));
        luna.answer("luna://com.palm.systemservice/getPreferences", () => ({ returnValue: true }));
        luna.answer("luna://com.palm.storage/erase/EraseVar", () => ({ returnValue: true }));
        const service = createDeviceInfoService(luna);
        service.onShown();
        await settle();
        service.onAskErase("eraseApps");
        assert.equal(service.getState().data.confirm?.kind, "eraseApps");
        service.onConfirmErase("ok");
        await settle();
        assert.ok(luna.calls.some((c) => c.uri.endsWith("EraseVar")));
    });
});
