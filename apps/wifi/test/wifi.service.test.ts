// The Wi-Fi card's state machine, against a fake com.palm.wifi.
//
// The scenarios are the ones the card before it had (apps/baseline/wifi-enyo on
// enyo's lib/wifi), and the payloads and replies are the ones
// services/nm-connectionmanager sends.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createFakeLuna, type FakeLuna } from "@webos/api/infra/luna/fake.service.ts";
import { canJoin, canSaveAddress, createWifiService, isAddress, joinFailureText, maskFor,
         SCAN_MS, type WifiService } from "../src/wifi.service.ts";
import type { Payload } from "@webos/api/infra/luna/service.ts";

const WIFI = "luna://com.palm.wifi/";
const GET_STATUS = `${WIFI}getstatus`;
const FIND = `${WIFI}findnetworks`;
const CONNECT = `${WIFI}connect`;

const settle = () => new Promise((resolve) => setImmediate(resolve));

// Three networks, as the service lists them: one joined, one secured and
// remembered, one secured and not.
const networks = (joinedState = "ipConfigured") => ({
    returnValue: true,
    foundNetworks: [
        { networkInfo: { ssid: "Casa", signalBars: 3, profileId: 10, connectState: joinedState } },
        { networkInfo: { ssid: "Oficina", signalBars: 2, securityType: "wpa-personal", profileId: 13 } },
        { networkInfo: { ssid: "Vecino", signalBars: 1, securityType: "wpa-personal" } },
    ],
});

interface Clock {
    readonly tick: () => void;
    readonly running: () => number;
}

const setup = () => {
    const luna = createFakeLuna();
    const logs: string[] = [];
    let timer: (() => void) | undefined;
    const clock: Clock = {
        tick: () => timer?.(),
        running: () => (timer ? 1 : 0),
    };
    const service = createWifiService({
        luna,
        setInterval: (callback, ms) => {
            assert.equal(ms, SCAN_MS, "the list rescans at HP's twelve seconds");
            timer = callback;
            return 1;
        },
        clearInterval: () => { timer = undefined; },
        log: (message) => logs.push(message),
    });
    luna.answer(FIND, () => networks());
    return { luna, service, logs, clock, data: () => service.getState().data };
};

const push = (luna: FakeLuna, reply: Payload) => {
    const status = luna.subscribers.find((one) => one.uri === GET_STATUS);
    assert.ok(status, "the card is watching com.palm.wifi");
    status.push({ returnValue: true, ...reply });
};

const payloads = (luna: FakeLuna, uri: string): Payload[] =>
    luna.calls.filter((call) => call.uri === uri).map((call) => call.payload);

describe("what Sign In accepts", () => {
    const fields = (over: Partial<Parameters<typeof canJoin>[0]>) =>
        ({ ssid: "Net", security: "wpa-personal", password: "", userName: "", keyIndex: 0, fixed: false, ...over }) as
            Parameters<typeof canJoin>[0];

    test("an open network needs only a name, and a name is at most 32 bytes", () => {
        assert.equal(canJoin(fields({ security: "none" })), true);
        assert.equal(canJoin(fields({ security: "none", ssid: "" })), false);
        assert.equal(canJoin(fields({ security: "none", ssid: "x".repeat(33) })), false);
    });

    test("WPA takes 8 to 63 characters, or 64 hex digits", () => {
        assert.equal(canJoin(fields({ password: "short" })), false);
        assert.equal(canJoin(fields({ password: "correcthorse" })), true);
        assert.equal(canJoin(fields({ password: "z".repeat(64) })), false, "64 characters that are not hex");
        assert.equal(canJoin(fields({ password: "ab".repeat(32) })), true, "64 hex digits");
    });

    test("WEP takes 5 or 13 characters, or 10 or 26 hex digits", () => {
        assert.equal(canJoin(fields({ security: "wep", password: "abcde" })), true);
        assert.equal(canJoin(fields({ security: "wep", password: "abcd" })), false);
        assert.equal(canJoin(fields({ security: "wep", password: "0123456789" })), true);
        assert.equal(canJoin(fields({ security: "wep", password: "0123456789ab" })), false);
    });

    test("enterprise needs a user name and a password", () => {
        assert.equal(canJoin(fields({ security: "enterprise", userName: "ana", password: "x" })), true);
        assert.equal(canJoin(fields({ security: "enterprise", userName: "ana" })), false);
        assert.equal(canJoin(fields({ security: "enterprise", password: "x" })), false);
    });
});

describe("what a failed join says", () => {
    test("HP's words, by the name the service gives the failure", () => {
        assert.match(joinFailureText("IncorrectPasskey"), /password you entered/);
        assert.match(joinFailureText("IncorrectPassword"), /username or password/);
        assert.match(joinFailureText("ApNotFound"), /No network of that name/);
        assert.match(joinFailureText("ServerCertificateRequired"), /security certificate/);
        // The service's own generic failure, and anything it may add later.
        assert.match(joinFailureText("AssociationFailed"), /Unable to connect/);
        assert.match(joinFailureText(undefined), /Unable to connect/);
    });
});

describe("the address screen's fields", () => {
    test("an address is four numbers under 256", () => {
        assert.equal(isAddress("10.20.30.40"), true);
        assert.equal(isAddress("255.255.255.255"), true);
        assert.equal(isAddress("10.20.30.256"), false);
        assert.equal(isAddress("10.20.30"), false);
        assert.equal(isAddress(""), false);
    });

    test("Done needs the address and the mask, and accepts the rest empty", () => {
        const fields = { automatic: false, ip: "10.0.0.2", subnet: "255.0.0.0", gateway: "", dns1: "", dns2: "" };
        assert.equal(canSaveAddress(fields), true);
        assert.equal(canSaveAddress({ ...fields, gateway: "10.0.0.1" }), true);
        assert.equal(canSaveAddress({ ...fields, gateway: "x" }), false);
        assert.equal(canSaveAddress({ ...fields, ip: "" }), false);
        assert.equal(canSaveAddress({ ...fields, subnet: "" }), false);
        assert.equal(canSaveAddress({ ...fields, automatic: true, ip: "", subnet: "" }), true,
                     "nothing to check when the address is automatic");
    });

    test("a mask is offered for the class the address is in", () => {
        assert.equal(maskFor("10.0.0.2"), "255.0.0.0");
        assert.equal(maskFor("172.16.0.2"), "255.255.0.0");
        assert.equal(maskFor("192.168.1.2"), "255.255.255.0");
        assert.equal(maskFor("not an address"), "");
    });
});

describe("the list", () => {
    test("it watches the radio, scans, and shows what was found", async () => {
        const { luna, service, data, clock } = setup();
        service.onShown();
        await settle();
        assert.equal(luna.subscribers.filter((one) => one.uri === GET_STATUS).length, 1);
        assert.deepEqual(data().networks.map((network) => network.ssid), ["Casa", "Oficina", "Vecino"]);
        assert.deepEqual(data().networks[1], { ssid: "Oficina", bars: 2, security: "wpa-personal", profileId: 13 });
        assert.equal(data().screen, "list");

        clock.tick();
        await settle();
        assert.equal(payloads(luna, FIND).length, 2, "and again at every tick of the scan");
    });

    test("a scan that fails says so instead of stopping the card", async () => {
        const { luna, service, data, logs } = setup();
        luna.answer(FIND, () => ({ returnValue: false, errorText: "no wifi device" }));
        service.onShown();
        await settle();
        assert.match(data().caption, /could not be read/);
        assert.equal(data().scanning, false);
        assert.equal(logs.length, 1);
    });

    test("the radio switch waits for the radio, and goes back when it is refused", async () => {
        const { luna, service, data } = setup();
        luna.answer(`${WIFI}setstate`, () => ({ returnValue: true }));
        service.onShown();
        await settle();

        service.onRadio(false);
        assert.equal(data().radioWanted, false, "the switch is where the user put it");
        assert.deepEqual(payloads(luna, `${WIFI}setstate`), [{ state: "disabled" }]);
        push(luna, { status: "serviceDisabled" });
        assert.equal(data().radio, false);
        assert.equal(data().radioWanted, undefined, "and usable again once the radio is there");
        assert.deepEqual(data().networks, [], "with nothing left to show");

        luna.answer(`${WIFI}setstate`, () => ({ returnValue: false, errorText: "no wifi device" }));
        service.onRadio(true);
        await settle();
        assert.equal(data().radioWanted, undefined, "a refusal puts the switch back");
        assert.equal(data().radio, false);
    });

    test("a network that is remembered is joined without asking again", async () => {
        const { luna, service, data } = setup();
        luna.answer(CONNECT, () => ({ returnValue: true, profileId: 13 }));
        service.onShown();
        await settle();
        service.onNetwork("Oficina");
        await settle();
        assert.deepEqual(payloads(luna, CONNECT), [{ profileId: 13 }]);
        assert.equal(data().networks.find((one) => one.ssid === "Oficina")?.connectState, "associating");
        assert.equal(data().screen, "list", "no screen for a network it already knows");
    });

    test("an open network is joined by name, and a secured one asks", async () => {
        const { luna, service, data } = setup();
        luna.answer(FIND, () => ({
            returnValue: true,
            foundNetworks: [
                { networkInfo: { ssid: "Abierta", signalBars: 2 } },
                { networkInfo: { ssid: "Vecino", signalBars: 1, securityType: "wpa-personal" } },
            ],
        }));
        luna.answer(CONNECT, () => ({ returnValue: true }));
        service.onShown();
        await settle();

        service.onNetwork("Abierta");
        await settle();
        assert.deepEqual(payloads(luna, CONNECT), [{ ssid: "Abierta" }]);
        assert.equal(data().screen, "list");

        service.onNetwork("Vecino");
        assert.equal(data().screen, "join");
        assert.deepEqual(data().join, { ssid: "Vecino", security: "wpa-personal", password: "", userName: "",
                                        keyIndex: 0, fixed: true });
        assert.equal(data().caption, "Join Vecino");
    });

    test("the joined network opens its address settings", async () => {
        const { luna, service, data } = setup();
        luna.answer(`${WIFI}getprofile`, () => ({
            returnValue: true,
            wifiProfile: { profileId: 10, ssid: "Casa", useStaticIp: false },
            ipInfo: { ip: "10.20.30.99", subnet: "255.255.255.0", gateway: "10.20.30.1", dns1: "10.20.30.1" },
        }));
        service.onShown();
        await settle();
        service.onNetwork("Casa");
        await settle();
        assert.deepEqual(payloads(luna, `${WIFI}getprofile`), [{ profileId: 10 }]);
        assert.equal(data().screen, "address");
        assert.deepEqual(data().address, { automatic: true, ip: "10.20.30.99", subnet: "255.255.255.0",
                                           gateway: "10.20.30.1", dns1: "10.20.30.1", dns2: "" });
    });
});

describe("joining", () => {
    const startJoin = async (service: WifiService, luna: FakeLuna) => {
        service.onShown();
        await settle();
        service.onNetwork("Vecino");
        service.onJoinField({ password: "correcthorse" });
        luna.answer(CONNECT, () => ({ returnValue: true, profileId: 20 }));
        service.onJoin();
        await settle();
    };

    test("Sign In sends the name, the security and the key", async () => {
        const { luna, service, data } = setup();
        await startJoin(service, luna);
        assert.deepEqual(payloads(luna, CONNECT), [{
            ssid: "Vecino",
            security: { securityType: "wpa-personal", simpleSecurity: { passKey: "correcthorse" } },
        }]);
        assert.equal(data().joining, true, "and says it is working");
    });

    test("a network that joins takes the card back to the list, as HP's did", async () => {
        const { luna, service, data } = setup();
        await startJoin(service, luna);
        push(luna, { status: "connectionStateChanged",
                     networkInfo: { ssid: "Vecino", connectState: "ipConfigured", profileId: 20 } });
        await settle();
        assert.equal(data().screen, "list");
        assert.equal(data().joining, false);
        assert.equal(data().joinMessage, "");
    });

    test("a wrong password is said in HP's words, and Sign In can be pressed again", async () => {
        const { luna, service, data } = setup();
        await startJoin(service, luna);
        push(luna, { status: "connectionStateChanged",
                     networkInfo: { ssid: "Vecino", connectState: "associationFailed",
                                    lastConnectError: "IncorrectPasskey" } });
        assert.equal(data().screen, "join");
        assert.match(data().joinMessage, /password you entered is not correct/);
        assert.equal(data().joining, false);
    });

    test("a refusal shows the service's own words", async () => {
        const { luna, service, data } = setup();
        service.onShown();
        await settle();
        service.onNetwork("Vecino");
        service.onJoinField({ password: "correcthorse" });
        luna.answer(CONNECT, () => ({ returnValue: false, errorText: "a WPA password is 8 to 63 characters" }));
        service.onJoin();
        await settle();
        assert.equal(data().joinMessage, "a WPA password is 8 to 63 characters");
        assert.equal(data().joining, false);
    });

    test("Join Network asks for a name too, and Cancel goes back to the list", async () => {
        const { service, data } = setup();
        service.onShown();
        await settle();
        service.onJoinOther();
        assert.equal(data().screen, "join");
        assert.equal(data().join?.fixed, false, "the name and the security are the user's to choose");
        assert.equal(data().caption, "Join Other Network");
        service.onCancelJoin();
        assert.equal(data().screen, "list");
    });
});

describe("the address screen", () => {
    const openAddress = async (luna: FakeLuna, service: WifiService, profile: Payload = {}) => {
        luna.answer(`${WIFI}getprofile`, () => ({
            returnValue: true,
            wifiProfile: { profileId: 10, ssid: "Casa", useStaticIp: true, ...profile },
            ipInfo: { ip: "10.20.30.99", subnet: "255.255.255.0", gateway: "10.20.30.1" },
        }));
        service.onShown();
        await settle();
        service.onNetwork("Casa");
        await settle();
    };

    test("it says what it is connected to, with the access point once it is known", async () => {
        const { luna, service, data } = setup();
        await openAddress(luna, service);
        push(luna, { status: "connectionStateChanged",
                     networkInfo: { ssid: "Casa", connectState: "ipConfigured", profileId: 10 },
                     apInfo: { bssid: "AA:BB:CC:DD:EE:FF", channel: 6 } });
        assert.equal(data().caption, "Connected to Casa. BSSID AA:BB:CC:DD:EE:FF, Channel 6.");
    });

    test("Done writes the address and goes back; a refusal is kept on screen", async () => {
        const { luna, service, data } = setup();
        await openAddress(luna, service);
        service.onAddressField({ automatic: false, subnet: "" });
        service.onAddressField({ ip: "10.0.0.2" });
        assert.equal(data().address?.subnet, "255.0.0.0", "an empty mask is filled in for the address's class");
        luna.answer(CONNECT, () => ({ returnValue: true }));
        service.onSaveAddress();
        await settle();
        assert.deepEqual(payloads(luna, CONNECT), [{
            profileId: 10, useStaticIp: true, ipAddress: "10.0.0.2", subnet: "255.0.0.0",
            gateway: "10.20.30.1", dns1: "", dns2: "",
        }], "the profile, and the address the user typed");
        assert.equal(data().screen, "list");

        await openAddress(luna, service);
        service.onAddressField({ automatic: false, gateway: "x" });
        assert.equal(canSaveAddress(data().address!), false, "and Done is not offered for an address that is not one");
        service.onAddressField({ gateway: "10.0.0.1" });
        luna.answer(CONNECT, () => ({ returnValue: false, errorText: "not a gateway address: x" }));
        service.onSaveAddress();
        await settle();
        assert.equal(data().caption, "The address settings were not applied: not a gateway address: x");
        assert.equal(data().screen, "address", "and the card stays where it is");
    });

    test("Forget deletes the profile and returns to the list", async () => {
        const { luna, service, data } = setup();
        await openAddress(luna, service);
        luna.answer(`${WIFI}deleteprofile`, () => ({ returnValue: true }));
        service.onForget();
        await settle();
        assert.deepEqual(payloads(luna, `${WIFI}deleteprofile`), [{ profileId: 10 }]);
        assert.equal(data().screen, "list");
    });
});

describe("known networks", () => {
    const profiles = {
        returnValue: true,
        profileList: [
            { wifiProfile: { profileId: 10, ssid: "Casa" } },
            { wifiProfile: { profileId: 13, ssid: "Oficina", security: { securityType: "wpa-personal" } } },
        ],
    };

    test("they are read when the screen opens, and again after a delete", async () => {
        const { luna, service, data } = setup();
        luna.answer(`${WIFI}getprofilelist`, () => profiles);
        luna.answer(`${WIFI}deleteprofile`, () => ({ returnValue: true }));
        service.onShown();
        await settle();
        service.onOpenKnown();
        await settle();
        assert.equal(data().screen, "known");
        assert.deepEqual(data().known?.map((one) => `${one.ssid}:${one.security}`), ["Casa:none", "Oficina:wpa-personal"]);

        service.onForgetKnown(13);
        await settle();
        assert.deepEqual(payloads(luna, `${WIFI}deleteprofile`), [{ profileId: 13 }]);
        assert.equal(payloads(luna, `${WIFI}getprofilelist`).length, 2, "read again once the delete has happened");
    });

    test("a list that could not be read is said; an empty one is simply empty", async () => {
        const { luna, service, data } = setup();
        luna.answer(`${WIFI}getprofilelist`, () => ({ returnValue: false, errorText: "no" }));
        service.onShown();
        await settle();
        service.onOpenKnown();
        await settle();
        assert.equal(data().knownUnreadable, true);

        luna.answer(`${WIFI}getprofilelist`, () => ({ returnValue: true, profileList: [] }));
        service.onOpenKnown();
        await settle();
        assert.deepEqual(data().known, []);
        assert.equal(data().knownUnreadable, false);
    });
});

describe("when the device sleeps", () => {
    const WAKE = "luna://com.palm.connectionmanager/getWakeOnWiFiMode";
    const SET_WAKE = "luna://com.palm.connectionmanager/setWakeOnWiFiMode";

    test("the setting follows the answer, not the tap", async () => {
        const { luna, service, data } = setup();
        luna.answer(WAKE, () => ({ returnValue: true, mode: "disable" }));
        service.onShown();
        await settle();
        service.onOpenSettings();
        await settle();
        assert.equal(data().screen, "settings");
        assert.equal(data().sleep, "disable");

        luna.answer(SET_WAKE, () => ({ returnValue: true, mode: "enable" }));
        service.onSleep("enable");
        await settle();
        assert.deepEqual(payloads(luna, SET_WAKE), [{ mode: "enable" }]);
        assert.equal(data().sleep, "enable");
    });

    test("a refused change goes back to what is stored", async () => {
        const { luna, service, data } = setup();
        luna.answer(WAKE, () => ({ returnValue: true, mode: "disable" }));
        luna.answer(SET_WAKE, () => ({ returnValue: false, errorText: 'expected {"mode": "enable" | "disable"}' }));
        service.onShown();
        await settle();
        service.onOpenSettings();
        await settle();
        service.onSleep("enable");
        await settle();
        assert.equal(data().sleep, "disable");
        assert.equal(payloads(luna, WAKE).length, 2, "and it asks the service again rather than guessing");
    });
});

describe("how the card is opened and left", () => {
    test("the menu opens a network it cannot join by itself on the join screen", async () => {
        const { service, data } = setup();
        service.onShown({ target: { ssid: "Oficina", securityType: "wpa-personal" } });
        await settle();
        assert.equal(data().screen, "join");
        assert.equal(data().join?.ssid, "Oficina");
        assert.equal(data().join?.fixed, true);
    });

    test("an open network the menu could not join opens the list, not a join screen", async () => {
        const { service, data } = setup();
        service.onShown({ target: { ssid: "Abierta", securityType: "" } });
        await settle();
        assert.equal(data().screen, "list");
    });

    test("the joined network opens its address settings, once the card knows which it is", async () => {
        const { luna, service, data } = setup();
        luna.answer(`${WIFI}getprofile`, () => ({
            returnValue: true, wifiProfile: { profileId: 10, ssid: "Casa", useStaticIp: false },
        }));
        service.onShown({ target: { ssid: "Casa", securityType: "", profileId: 10, connectState: "ipConfigured" } });
        await settle();
        assert.equal(data().screen, "address");
        assert.deepEqual(payloads(luna, `${WIFI}getprofile`), [{ profileId: 10 }]);
    });

    test("back comes out of each screen, and only then closes the card", async () => {
        const { service } = setup();
        service.onShown();
        await settle();
        assert.equal(service.onBack(), false, "on the list, back is the card closing");
        service.onJoinOther();
        assert.equal(service.onBack(), true);
        assert.equal(service.getState().data.screen, "list");
    });

    test("sent away it stops watching and stops scanning; shown again it starts", async () => {
        const { luna, service, clock } = setup();
        service.onShown();
        await settle();
        assert.equal(clock.running(), 1);
        service.onHidden();
        assert.deepEqual(luna.subscribers, []);
        assert.equal(clock.running(), 0);
        service.onShown();
        await settle();
        assert.equal(luna.subscribers.length, 1);
        assert.equal(clock.running(), 1);
        service.dispose();
        assert.deepEqual(luna.subscribers, []);
        assert.equal(clock.running(), 0);
    });
});
