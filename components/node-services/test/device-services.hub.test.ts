// com.palm.deviceprofile, com.palm.accountservices,
// com.palm.service.customization and com.palm.devicewipe on a private hub,
// and the kit's serveOnDemand they share.

import assert from "node:assert/strict";
import { spawn, type ChildProcess } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { after, before, beforeEach, describe, test } from "node:test";
import { setTimeout as sleep } from "node:timers/promises";

import { createActivity, isLunaError, openBus, type Bus, type Payload } from "#kit/luna.ts";
import { serveOnDemand, type OnDemandService } from "#kit/mojoservice.ts";
import { ACCOUNT_METHODS, SERVER_METHODS, TOKEN_METHODS } from "../services/com.palm.accountservices/commands.ts";
import { createAccountServices, SERVICE_NAME as ACCOUNTS } from "../services/com.palm.accountservices/service.ts";
import { METHODS as CUSTOMIZATION_METHODS } from "../services/com.palm.service.customization/commands.ts";
import { createCustomizationService, SERVICE_NAME as CUSTOMIZATION } from "../services/com.palm.service.customization/service.ts";
import { createDeviceProfileService, SERVICE_NAME as PROFILE } from "../services/com.palm.deviceprofile/service.ts";
import { createDevicewipeService, SERVICE_NAME as WIPE } from "../services/com.palm.devicewipe/service.ts";
import { startTestBus, type TestBus } from "./hub.ts";

const SYSTEM = "com.palm.systemservice";
const ON_DEMAND = "com.webosce.test.ondemand";
const PROFILE_MAIN = new URL("../services/com.palm.deviceprofile/main.ts", import.meta.url).pathname;

const env = {} as {
    hub: TestBus;
    client: Bus;
    system: Bus;
    services: OnDemandService[];
    exits: Record<string, number>;
    logs: string[];
    preferences: Payload[];
    systemFails: boolean;
};

const activity = () => createActivity({
    setTimer: (callback, ms) => setTimeout(callback, ms),
    clearTimer: (timer) => clearTimeout(timer as ReturnType<typeof setTimeout> | undefined),
});

const deps = (name: string) => ({
    openBus,
    createActivity: activity,
    // Long enough not to go away in the middle of a test.
    idleMs: 60_000,
    exit: () => { env.exits[name] = (env.exits[name] ?? 0) + 1; },
    log: (message: string) => env.logs.push(message),
});

const failsWith = (fields: Payload) => (error: unknown) => {
    assert.ok(isLunaError(error), String(error));
    for (const [key, value] of Object.entries(fields)) {
        assert.deepEqual((error as unknown as Payload)[key], value, key);
    }
    return true;
};

const BUILD = "BUILDNAME=webOS-CE\nBUILDNUMBER=3.0.5-0+1.test\nBUILDTIME=20260917000000\nPRODUCT_VERSION_STRING=webOS Community Edition 3.0.5-0+1.test\n";

before(async () => {
    env.hub = await startTestBus({ services: [PROFILE, ACCOUNTS, CUSTOMIZATION, WIPE, SYSTEM, ON_DEMAND] });
    env.exits = {};
    env.logs = [];
    env.preferences = [];
    env.systemFails = false;
    env.system = openBus(SYSTEM);
    env.system.method("setPreferences", ({ payload }) => {
        if (env.systemFails) {
            return { returnValue: false, errorCode: -1, errorText: "refused" };
        }
        env.preferences.push(payload);
        return {};
    });
    env.services = [
        createAccountServices(deps(ACCOUNTS))(),
        createCustomizationService(deps(CUSTOMIZATION))(),
        createDevicewipeService(deps(WIPE))(),
    ];
    env.client = openBus(null);
});

after(() => {
    for (const service of env.services ?? []) {
        service.close();
    }
    env.client?.close();
    env.system?.close();
    env.hub?.stop();
});

beforeEach(() => {
    env.preferences.length = 0;
    env.systemFails = false;
    env.logs.length = 0;
});

describe("com.palm.deviceprofile", () => {
    const facts = { nduId: "cd".repeat(20), model: "Test Model", hardwareVersion: "Rev B", machine: "x86_64", buildInfo: BUILD };

    test("getDeviceProfile answers {deviceInfo}, and getDeviceId the nduId", async () => {
        const service = createDeviceProfileService({ ...deps(PROFILE), facts: () => facts })();
        try {
            const { deviceInfo } = await env.client.call<{ deviceInfo: Payload }>(`luna://${PROFILE}/getDeviceProfile`, {});
            assert.equal(deviceInfo.nduId, facts.nduId);
            assert.equal(deviceInfo.deviceModel, "Test Model");
            assert.equal(deviceInfo.softwareVersion, "webOS-CE-3.0.5-0+1.test");
            const reply = await env.client.call(`luna://${PROFILE}/getDeviceId`, {});
            assert.equal(reply.deviceId, facts.nduId);
        } finally {
            service.close();
        }
    });

    test("main.ts keeps one nduId for the install, and reads this build", async () => {
        const dir = mkdtempSync(join(tmpdir(), "deviceprofile-"));
        const nduid = join(dir, "data", "nduid");
        writeFileSync(join(dir, "build-info"), BUILD);
        const run = async () => {
            const child: ChildProcess = spawn(process.execPath, [PROFILE_MAIN], {
                env: { ...process.env, WEBOS_NDUID_FILE: nduid, WEBOS_BUILD_INFO: join(dir, "build-info") },
                stdio: ["ignore", "ignore", "inherit"],
            });
            const exited = new Promise<number | null>((resolve) => child.on("exit", (code) => resolve(code)));
            try {
                let reply: Payload | undefined;
                for (let i = 0; i < 100 && !reply; i++) {
                    reply = await env.client.call(`luna://${PROFILE}/getDeviceProfile`, {}, { timeout: 1 }).catch(() => undefined);
                    if (!reply) {
                        await sleep(50);
                    }
                }
                assert.ok(reply, "the service never answered");
                await env.client.call(`luna://${PROFILE}/__quit`);
                assert.equal(await exited, 0);
                return reply.deviceInfo as Payload;
            } finally {
                child.kill("SIGKILL");
            }
        };
        const first = await run();
        assert.match(String(first.nduId), /^[0-9a-f]{40}$/);
        assert.equal(readFileSync(nduid, "utf8").trim(), first.nduId);
        assert.equal(first.softwareVersion, "webOS-CE-3.0.5-0+1.test");
        assert.equal(first.hardwareType, process.arch === "x64" ? "x86_64" : first.hardwareType);
        const second = await run();
        assert.equal(second.nduId, first.nduId);
        assert.ok(existsSync(nduid));
    });
});

describe("com.palm.accountservices", () => {
    const A = `luna://${ACCOUNTS}`;

    test("there is no webOS account: no token, no account, no servers", async () => {
        for (const method of TOKEN_METHODS) {
            await assert.rejects(env.client.call(`${A}/${method}`, {}), failsWith({ errorCode: "NO_TOKEN" }), method);
        }
        for (const method of ACCOUNT_METHODS) {
            await assert.rejects(env.client.call(`${A}/${method}`, {}),
                failsWith({ errorCode: "ACCOUNT_NOT_DEFINED_ERROR", errorText: "Could not get local account info" }), method);
        }
        for (const method of SERVER_METHODS) {
            await assert.rejects(env.client.call(`${A}/${method}`, {}), failsWith({ errorCode: "SERVER_UNAVAILABLE" }), method);
        }
    });

    test("the palmID screens' calls fail the way their error popup expects", async () => {
        await assert.rejects(env.client.call(`${A}/getAggregatedAccountInfo`, { locale: "en_US" }),
            failsWith({ errorCode: "ACCOUNT_NOT_DEFINED_ERROR" }));
        await assert.rejects(env.client.call(`${A}/isUserValid`, { email: "a@b", deviceId: "d", password: "p" }),
            failsWith({ errorCode: "SERVER_UNAVAILABLE" }));
    });

    test("updateDeviceProperties hands its arguments to the system preferences, as LunaSysMgr expects", async () => {
        await env.client.call(`${A}/updateDeviceProperties`, { novacomEnabled: "false" });
        assert.deepEqual(env.preferences, [{ novacomEnabled: "false" }]);
    });

    test("updateDeviceProperties refuses nothing to update, and reports a refused update", async () => {
        await assert.rejects(env.client.call(`${A}/updateDeviceProperties`, {}), failsWith({ errorCode: "INVALID_REQUEST" }));
        env.systemFails = true;
        await assert.rejects(env.client.call(`${A}/updateDeviceProperties`, { novacomEnabled: "true" }),
            failsWith({ errorCode: "UPDATE_ERROR" }));
        assert.ok(env.logs.some((line) => line.includes("refused")));
        assert.deepEqual(env.preferences, []);
    });

    test("what needs no account answers", async () => {
        assert.equal((await env.client.call(`${A}/getPreferences`, {})).returnValue, true);
        assert.equal((await env.client.call(`${A}/processMessage`, { node: "x" })).returnValue, true);
        assert.equal((await env.client.call(`${A}/notifyAuthenticationFailure`, {})).returnValue, true);
    });
});

describe("com.palm.service.customization", () => {
    test("every command answers, with nothing to customize", async () => {
        for (const method of CUSTOMIZATION_METHODS) {
            const reply = await env.client.call(`luna://${CUSTOMIZATION}/${method}`, { language: "en", country: "us" });
            assert.equal(reply.returnValue, true, method);
        }
        assert.equal(env.logs.length, CUSTOMIZATION_METHODS.length);
        assert.ok(env.logs[0]!.includes("nothing to customize"));
    });
});

describe("com.palm.devicewipe", () => {
    test("a wipe cannot be issued without the account servers", async () => {
        await assert.rejects(env.client.call(`luna://${WIPE}/deviceWipe`, { item: "x", node: "/a/b" }),
            failsWith({ errorCode: "WIPE_NOT_ISSUED" }));
        assert.ok(env.logs.some((line) => line.includes("refused")));
    });

    test("registerPubSub answers", async () => {
        assert.equal((await env.client.call(`luna://${WIPE}/registerPubSub`, {})).returnValue, true);
    });
});

describe("serveOnDemand", () => {
    test("leaves once idle, and __quit leaves after answering", async () => {
        const events: string[] = [];
        const service = serveOnDemand({
            openBus, createActivity: activity, idleMs: 400,
            exit: () => events.push("exit"),
        })(ON_DEMAND, [{ name: "ping", handler: () => ({ pong: true }) }]);
        try {
            assert.equal((await env.client.call(`luna://${ON_DEMAND}/ping`, {})).pong, true);
            assert.deepEqual(events, []);
            await sleep(600);
            assert.deepEqual(events, ["exit"]);
            const reply = await env.client.call(`luna://${ON_DEMAND}/__quit`, {});
            assert.equal(reply.returnValue, true);
            // After the quit delay (100 ms), before the idle timer could fire again.
            await sleep(250);
            assert.deepEqual(events, ["exit", "exit"]);
        } finally {
            service.close();
        }
    });
});
