// com.palm.deviceprofile's pure part.

import assert from "node:assert/strict";
import { test } from "node:test";

import { deviceIdOf, deviceInfo, newNduId, parseBuildInfo } from "../services/com.palm.deviceprofile/profile.ts";

const BUILD = "PRODUCT_VERSION_STRING=webOS Community Edition 3.0.5-0+400.abc\nBUILDNAME=webOS-CE\nBUILDNUMBER=3.0.5-0+400.abc\nBUILDTIME=20260917120000\n";

test("the build info is read as HP's KEY=VALUE lines", () => {
    assert.deepEqual(parseBuildInfo("A=1\n\nnot a pair\n B = two words \nC=x=y\n"), { A: "1", B: "two words", C: "x=y" });
});

test("deviceInfo carries the host's model and this build, and leaves the modem's fields empty", () => {
    const info = deviceInfo({ nduId: "ab".repeat(20), model: "ZBook", hardwareVersion: "V3", machine: "x86_64", buildInfo: BUILD });
    assert.equal(info.nduId, "ab".repeat(20));
    assert.equal(info.deviceModel, "ZBook");
    assert.equal(info.hardwareVersion, "V3");
    assert.equal(info.hardwareType, "x86_64");
    assert.equal(info.softwareVersion, "webOS-CE-3.0.5-0+400.abc");
    assert.equal(info.carrierROM, info.softwareVersion);
    assert.equal(info.softwareBuildBranch, "webOS Community Edition 3.0.5-0+400.abc");
    assert.equal(info.buildTime, "20260917120000");
    assert.equal(info.platform, "Nova");
    for (const field of ["deviceId", "phoneNumber", "carrier", "firmwareVersion", "serialNumber", "WIFIoADDR", "homeMcc"]) {
        assert.equal(info[field], "", field);
    }
    assert.equal(Object.keys(info).length, 33);
});

test("without build info the versions are empty, not half made", () => {
    const info = deviceInfo({ nduId: "", model: "", hardwareVersion: "", machine: "", buildInfo: "" });
    assert.equal(info.softwareVersion, "");
    assert.equal(info.buildTime, "");
});

test("getDeviceId is the modem's id, or the nduId without one", () => {
    assert.equal(deviceIdOf({ deviceId: "IMEI:1", nduId: "n" }), "IMEI:1");
    assert.equal(deviceIdOf({ deviceId: "", nduId: "n" }), "n");
});

test("an nduId is 40 hex digits", () => {
    assert.equal(newNduId((n) => new Uint8Array(n).fill(0xab)), "ab".repeat(20));
    assert.match(newNduId((n) => Uint8Array.from({ length: n }, (_, i) => i)), /^[0-9a-f]{40}$/);
});
