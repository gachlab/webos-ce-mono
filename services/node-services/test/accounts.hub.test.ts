// com.palm.service.accounts on a private hub with a real db8: HP's own test
// cases (tests/accounts-test.js) and the rest of the commands, with the
// transports, validator and system service played by this file.

import assert from "node:assert/strict";
import { mkdirSync, readdirSync, readFileSync, statSync, writeFileSync, cpSync } from "node:fs";
import { join } from "node:path";
import { after, before, beforeEach, describe, test } from "node:test";
import { setTimeout as sleep } from "node:timers/promises";

import { createDb8, type Db8 } from "#kit/db8.ts";
import { createActivity, isLunaError, openBus, type Bus, type Payload } from "#kit/luna.ts";
import type { Schema } from "#kit/json-schema.ts";
import { createAccountsService, SERVICE_NAME, type RunningService } from "../services/com.palm.service.accounts/service.ts";
import { CONFIGURATOR, startTestBus, type TestBus } from "./hub.ts";

const HP = new URL("../../../components/app-services/com.palm.service.accounts/", import.meta.url).pathname;
const PROFILE = new URL("../../../components/app-services/account-templates/palmprofile/com.palm.palmprofile/", import.meta.url).pathname;
const SERVICE = `luna://${SERVICE_NAME}`;

const TRANSPORT = "com.webosce.test.transport";
const APP = "com.webosce.test.app";
const STRANGER = "com.webosce.test.stranger";
const SYSTEM = "com.palm.systemservice";
const ACCOUNTS_APP = "com.palm.app.accounts";
const T = `luna://${TRANSPORT}`;
const MAIL = "com.webosce.test.mail";

const mailTemplate = {
    templateId: MAIL,
    loc_name: "Test Mail",
    validator: `${T}/validate`,
    onCapabilitiesChanged: `${T}/capabilitiesChanged`,
    onCredentialsChanged: `${T}/credentialsChanged`,
    readPermissions: [`${APP}*`],
    writePermissions: [APP],
    icon: { loc_32x32: "images/mail.png" },
    capabilityProviders: [
        { id: `${MAIL}.mail`, capability: "MAIL", onCreate: `${T}/created`, onEnabled: `${T}/enabled`,
          onDelete: `${T}/deleted`, onCredentialsChanged: `${T}/mailCredentialsChanged` },
        { id: `${MAIL}.contacts`, capability: "CONTACTS", onEnabled: `${T}/enabled`, alwaysOn: true },
        { id: `${MAIL}.calendar`, capability: "CALENDAR", onEnabled: `${T}/enabled` },
    ],
};

interface Env {
    hub: TestBus;
    templates: string;
    admin: Bus;
    fakes: Bus[];
    app: Bus;
    stranger: Bus;
    db: Db8;
    tempdb: Db8;
    service: RunningService;
    exits: number;
    flagged: number;
    flagFails: boolean;
    accountsApp: Bus;
    calls: { method: string; payload: Payload }[];
    failing: Set<string>;
    // Methods that wait for the test to release them.
    holding: Map<string, Promise<void>>;
    locale: { languageCode: string; countryCode: string };
    pushLocale?: () => void;
}
const env = {} as Env;

const schema = (name: string): Schema => JSON.parse(readFileSync(join(HP, "schemas", name), "utf8")) as Schema;

const kinds = (dir: string) => readdirSync(dir).map((file) => JSON.parse(readFileSync(join(dir, file), "utf8")) as Payload);

const until = async (condition: () => boolean | Promise<boolean>, what: string, ms = 4000) => {
    for (const started = Date.now(); Date.now() - started < ms; await sleep(20)) {
        if (await condition()) {
            return;
        }
    }
    assert.fail(`timed out waiting for ${what}`);
};

// A test's own calls: a transport hears of an account after createAccount has
// answered, so an earlier test's account can still be heard of in this one.
const calledWith = (method: string, accountId?: unknown) => env.calls
    .filter((call) => call.method === method && (accountId === undefined || call.payload.accountId === accountId))
    .map((call) => call.payload);

const failsWith = (fields: Payload) => (error: unknown) => {
    assert.ok(isLunaError(error), String(error));
    for (const [key, value] of Object.entries(fields)) {
        if (value instanceof RegExp) {
            assert.match(String((error as unknown as Payload)[key]), value, key);
        } else {
            assert.deepEqual((error as unknown as Payload)[key], value, key);
        }
    }
    return true;
};

before(async () => {
    env.hub = await startTestBus({ services: [SERVICE_NAME, TRANSPORT, APP, STRANGER, SYSTEM, ACCOUNTS_APP], db8: true });
    env.admin = openBus(CONFIGURATOR);
    await env.hub.startDb8(async () => {
        try {
            await env.admin.call("luna://com.palm.db/reserveIds", { count: 1 }, { timeout: 1 });
            return true;
        } catch {
            return false;
        }
    });
    env.db = createDb8(env.admin);
    env.tempdb = createDb8(env.admin, "com.palm.tempdb");
    // The configurator takes a kind's owner from its directory when it has none.
    for (const kind of kinds(join(HP, "db/kinds/com.palm.service.accounts"))) {
        await env.db.putKind({ owner: SERVICE_NAME, ...kind });
    }
    for (const kind of kinds(join(HP, "tempdb/kinds/com.palm.service.accounts"))) {
        await env.tempdb.putKind({ ...kind, owner: SERVICE_NAME });
    }

    // The templates: HP's profile and a test mail template.
    env.templates = join(env.hub.dir, "templates");
    cpSync(PROFILE, join(env.templates, "com.palm.palmprofile"), { recursive: true });
    mkdirSync(join(env.templates, "mail"), { recursive: true });
    writeFileSync(join(env.templates, "mail", "mail.json"), JSON.stringify(mailTemplate));

    env.calls = [];
    env.failing = new Set();
    env.holding = new Map();
    env.locale = { languageCode: "en", countryCode: "us" };

    // Transports and validator: record every call, fail the named ones.
    const transport = openBus(TRANSPORT);
    for (const method of ["validate", "created", "enabled", "deleted", "credentialsChanged", "mailCredentialsChanged",
        "capabilitiesChanged"]) {
        transport.method(method, async ({ payload }) => {
            env.calls.push({ method, payload });
            await env.holding.get(method);
            if (env.failing.has(method)) {
                return { returnValue: false, errorCode: 1, errorText: `${method} refused` };
            }
            return method === "validate" && payload.password === "right"
                ? { credentials: { token: `token-for-${String(payload.username)}` }, config: { server: "mail.test" } }
                : {};
        });
    }

    const system = openBus(SYSTEM);
    env.fakes = [transport, system];
    system.method("getPreferences", async function* ({ payload, signal }) {
        const keys = payload.keys as string[];
        const prefs = () => Object.fromEntries(keys.map((key) =>
            [key, key === "locale" ? { ...env.locale } : key === "deviceName" ? "Test Pad" : undefined]));
        yield prefs();
        while (!signal.aborted) {
            await new Promise<void>((resolve) => {
                env.pushLocale = resolve;
                signal.addEventListener("abort", () => resolve(), { once: true });
            });
            if (!signal.aborted) {
                yield prefs();
            }
        }
    });

    env.app = openBus(APP);
    env.stranger = openBus(STRANGER);
    env.exits = 0;
    env.flagged = 0;
    env.flagFails = false;
    env.accountsApp = openBus(ACCOUNTS_APP);
    env.service = await createAccountsService({
        openBus,
        createActivity: () => createActivity({
            setTimer: (callback, ms) => setTimeout(callback, ms),
            clearTimer: (timer) => clearTimeout(timer as ReturnType<typeof setTimeout> | undefined),
        }),
        files: {
            list: (dir) => { try { return readdirSync(dir); } catch { return []; } },
            isDirectory: (path) => { try { return statSync(path).isDirectory(); } catch { return false; } },
            isFile: (path) => { try { return statSync(path).isFile(); } catch { return false; } },
            read: (path) => { try { return readFileSync(path, "utf8"); } catch { return undefined; } },
        },
        templateRoots: [env.templates],
        schemas: { file: schema("template-file.json"), template: schema("template.json") },
        sleep: (ms) => sleep(Math.min(ms, 50)),
        delays: { beforeNotifyCreated: 0, afterMarkDeleted: 0, beforeNotifyDeleted: 0 },
        idleMs: 60_000,
        markProfileCreated: () => {
            if (env.flagFails) {
                throw new Error("EACCES: permission denied");
            }
            env.flagged++;
        },
        exit: () => { env.exits++; },
        log: process.env.WEBOS_TEST_LOGS ? (message) => console.log(`[accounts] ${message}`) : () => {},
    })();
});

after(() => {
    env.service?.close();
    for (const bus of [env.app, env.stranger, env.admin, env.accountsApp, ...(env.fakes ?? [])]) {
        bus?.close();
    }
    env.hub?.stop();
});

beforeEach(async () => {
    env.calls.length = 0;
    env.failing.clear();
    env.holding.clear();
    await env.db.delWhere({ from: "com.palm.account:1" }, { purge: true });
    await env.db.delWhere({ from: "com.palm.account.credentials:1" }, { purge: true });
    await env.tempdb.delWhere({ from: "com.palm.account.syncstate:1" }, { purge: true });
});

const create = (fields: Payload = {}, caller: Bus = env.app) => caller.call<{ result: Payload }>(`${SERVICE}/createAccount`, {
    templateId: MAIL,
    capabilityProviders: [{ id: `${MAIL}.mail` }, { id: `${MAIL}.contacts` }],
    username: "me@test",
    password: "right",
    ...fields,
});

// An account whose creation the transports have heard of (notifyAccountCreated
// runs after createAccount answers), with the calls so far forgotten.
const created = async (fields: Payload = {}) => {
    const { result } = await create(fields);
    await until(() => env.calls.some((call) => call.method === "capabilitiesChanged" && call.payload.accountId === result._id),
        "the transports to hear of the new account");
    env.calls.length = 0;
    return { result };
};

const PROFILE_QUERY = {
    from: "com.palm.account:1",
    where: [{ prop: "beingDeleted", op: "=", val: false }, { prop: "templateId", op: "=", val: "com.palm.palmprofile" }],
};

const accountIn = async (id: string) => (await env.db.get([id]))[0];

describe("templates", () => {
    test("both templates are listed, by name, with absolute icons", async () => {
        const { results } = await env.app.call<{ results: Payload[] }>(`${SERVICE}/listAccountTemplates`);
        assert.deepEqual(results.map((t) => t.templateId), ["com.palm.palmprofile", MAIL]);
        assert.deepEqual(results[1]!.icon, { loc_32x32: join(env.templates, "mail", "images/mail.png") });
    });

    test("filtered by one capability or several", async () => {
        const one = await env.app.call<{ results: Payload[] }>(`${SERVICE}/listAccountTemplates`, { capability: "MAIL" });
        assert.deepEqual(one.results.map((t) => t.templateId), [MAIL]);
        const several = await env.app.call<{ results: Payload[] }>(`${SERVICE}/listAccountTemplates`,
            { capability: ["MAIL", "PHONE"] });
        assert.deepEqual(several.results.map((t) => t.templateId), ["com.palm.palmprofile", MAIL]);
    });

    test("the list is announced in tempdb for apps to watch", async () => {
        const { results } = await env.tempdb.find({ from: "com.palm.signaling:1" });
        assert.deepEqual(results.map((r) => [r.appId, r.templates]),
            [["com.palm.accounts.templates", `com.palm.palmprofile,${MAIL}`]]);
    });

    test("appsChanged reloads them and updates the announcement", async () => {
        const extra = join(env.templates, "extra");
        mkdirSync(extra, { recursive: true });
        writeFileSync(join(extra, "t.json"), JSON.stringify({ templateId: "com.webosce.test.extra", loc_name: "Extra", capabilityProviders: [] }));
        await env.app.call(`${SERVICE}/appsChanged`);
        const { results } = await env.app.call<{ results: Payload[] }>(`${SERVICE}/listAccountTemplates`);
        assert.ok(results.some((t) => t.templateId === "com.webosce.test.extra"));
        await until(async () => (await env.tempdb.find({ from: "com.palm.signaling:1" })).results[0]?.templates
            === `com.palm.palmprofile,com.webosce.test.extra,${MAIL}`, "the new announcement");
    });
});

describe("createAccount", () => {
    test("validates the password, stores the account and the validator's credentials", async () => {
        const { result } = await create({ alias: "Work", config: { fromCaller: 1 } });
        assert.deepEqual(calledWith("validate"), [{
            username: "me@test", password: "right", templateId: MAIL, config: { fromCaller: 1 },
        }]);
        assert.equal(typeof result._id, "string");
        const stored = await accountIn(String(result._id));
        assert.equal(stored!.username, "me@test");
        assert.equal(stored!.alias, "Work");
        assert.equal(stored!.beingDeleted, false);
        assert.deepEqual((stored!.capabilityProviders as Payload[]).map((p) => [p.id, p.capability]),
            [[`${MAIL}.mail`, "MAIL"], [`${MAIL}.contacts`, "CONTACTS"]]);
        const read = await env.app.call(`${SERVICE}/readCredentials`, { accountId: result._id, name: "token" });
        assert.equal(read.credentials, "token-for-me@test");
    });

    test("then tells the transports, with the validator's config", async () => {
        const { result } = await create();
        await until(() => calledWith("created", result._id).length === 1, "onCreate");
        assert.deepEqual(calledWith("created", result._id), [{ accountId: result._id, config: { server: "mail.test" } }]);
        await until(() => calledWith("capabilitiesChanged").some((p) => p.accountId === result._id), "onCapabilitiesChanged");
        assert.deepEqual(calledWith("capabilitiesChanged").find((p) => p.accountId === result._id), {
            accountId: result._id,
            capabilityProviders: [
                { id: `${MAIL}.mail`, enabled: true }, { id: `${MAIL}.contacts`, enabled: true },
                { id: `${MAIL}.calendar`, enabled: false },
            ],
        });
    });

    test("with credentials instead of a password, stores them without validating", async () => {
        const { result } = await create({ password: undefined, credentials: { common: { password: "p" } } });
        assert.equal(calledWith("validate").length, 0);
        const read = await env.app.call(`${SERVICE}/readCredentials`, { accountId: result._id, name: "common" });
        assert.deepEqual(read.credentials, { password: "p" });
    });

    test("a failing validator is only logged: the account is made without credentials", async () => {
        env.failing.add("validate");
        const { result } = await created();
        const has = await env.app.call(`${SERVICE}/hasCredentials`, { accountId: result._id });
        assert.equal(has.value, false);
    });

    test("a second account with the same user and template is refused", async () => {
        await created();
        await assert.rejects(create(), failsWith({ errorCode: "DUPLICATE_ACCOUNT", errorText: "Unable to create a duplicate account" }));
    });

    test("missing arguments fail the way mojoservice answered", async () => {
        await assert.rejects(env.app.call(`${SERVICE}/createAccount`, {}),
            failsWith({ errorCode: -9999, errorText: "MojoService: no errorCode supplied missing templateId" }));
        await assert.rejects(create({ credentials: { a: 1 } }),
            failsWith({ errorText: "MojoService: no errorCode supplied can't have both password and credentials" }));
        await assert.rejects(create({ templateId: "com.webosce.test.none" }),
            failsWith({ errorText: /no account template for id=com.webosce.test.none/ }));
        await assert.rejects(create({ password: undefined }), failsWith({ errorText: /Assert.require failed/ }));
    });

    test("a caller the template does not name is refused", async () => {
        await assert.rejects(create({}, env.stranger), failsWith({
            errorCode: "",
            errorText: `Permission denied! ${STRANGER} is not specified in template ${MAIL} writePermissions`,
        }));
    });

    test("providers the template lacks, or repeated, are dropped", async () => {
        const { result } = await created({ capabilityProviders: [{ id: `${MAIL}.mail` }, { id: `${MAIL}.mail` }, { id: "nope" }] });
        assert.deepEqual(((await accountIn(String(result._id)))!.capabilityProviders as Payload[]).map((p) => p.id), [`${MAIL}.mail`]);
    });
});

describe("reading accounts", () => {
    test("getAccountInfo gives the account with its template woven in", async () => {
        const { result } = await created();
        const info = await env.app.call<{ result: Payload }>(`${SERVICE}/getAccountInfo`, { accountId: result._id });
        assert.equal(info.result.loc_name, "Test Mail");
        assert.equal(info.result.username, "me@test");
        assert.equal((info.result.capabilityProviders as Payload[])[0]!.onCreate, `${T}/created`);
        await assert.rejects(env.stranger.call(`${SERVICE}/getAccountInfo`, { accountId: result._id }),
            failsWith({ errorCode: "", errorText: /Permission denied/ }));
    });

    test("listAccounts by template or capability, without accounts being deleted", async () => {
        const { result: first } = await created();
        const { result: second } = await created({ username: "other@test", capabilityProviders: [{ id: `${MAIL}.calendar` }] });
        await env.db.merge([{ _id: String(second._id), beingDeleted: true }]);
        const byTemplate = await env.app.call<{ results: Payload[] }>(`${SERVICE}/listAccounts`, { templateId: MAIL });
        assert.deepEqual(byTemplate.results.map((a) => a._id), [first._id]);
        const byCapability = await env.app.call<{ results: Payload[] }>(`${SERVICE}/listAccounts`, { capability: "CONTACTS" });
        assert.deepEqual(byCapability.results.map((a) => a._id), [first._id]);
        const all = await env.app.call<{ results: Payload[] }>(`${SERVICE}/listAccounts`);
        assert.equal(all.results[0]!.loc_name, "Test Mail");
    });

    test("an account whose template is gone is left out of the list", async () => {
        await env.db.put([{ _kind: "com.palm.account:1", templateId: "com.webosce.test.gone", username: "x",
            beingDeleted: false, capabilityProviders: [] }]);
        const all = await env.app.call<{ results: Payload[] }>(`${SERVICE}/listAccounts`);
        assert.deepEqual(all.results, []);
    });

    test("empty filters are no filters, as in HP's service", async () => {
        const { result } = await create();
        const all = await env.app.call<{ results: Payload[] }>(`${SERVICE}/listAccounts`, { templateId: "" });
        assert.deepEqual(all.results.map((a) => a._id), [result._id]);
        const unfiltered = await env.app.call<{ results: Payload[] }>(`${SERVICE}/listAccountTemplates`);
        const templates = await env.app.call<{ results: Payload[] }>(`${SERVICE}/listAccountTemplates`, { capability: null });
        assert.deepEqual(templates.results, unfiltered.results);
    });

    test("the public variants are for whitelisted apps only", async () => {
        await assert.rejects(env.app.call(`${SERVICE}/listAccountsPublic`, { capability: "documents" }),
            failsWith({ errorCode: "", errorText: "Permission denied." }));
        await assert.rejects(env.app.call(`${SERVICE}/readCredentialsPublic`, { accountId: "x", name: "y" }),
            failsWith({ errorText: "Permission denied." }));
    });
});

describe("credentials", () => {
    test("writeCredentials stores and tells the transports; hasCredentials sees it", async () => {
        const { result } = await created({ password: undefined, credentials: {} });
        assert.equal((await env.app.call(`${SERVICE}/hasCredentials`, { accountId: result._id })).value, false);
        await env.app.call(`${SERVICE}/writeCredentials`, { accountId: result._id, name: "oauth", credentials: { t: 1 } });
        await env.app.call(`${SERVICE}/writeCredentials`, { accountId: result._id, name: "oauth", credentials: { t: 2 } });
        assert.equal((await env.app.call(`${SERVICE}/hasCredentials`, { accountId: result._id })).value, true);
        const read = await env.app.call(`${SERVICE}/readCredentials`, { accountId: result._id, name: "oauth" });
        assert.deepEqual(read.credentials, { t: 2 });
        const stored = await env.db.find({ from: "com.palm.account.credentials:1" });
        assert.equal(stored.results.length, 1, "a rewrite replaces the stored object");
        await until(() => calledWith("credentialsChanged", result._id).length === 2, "onCredentialsChanged");
        assert.deepEqual(calledWith("credentialsChanged", result._id)[0], { accountId: result._id });
    });

    test("reading a credential that is not there fails", async () => {
        const { result } = await created();
        await assert.rejects(env.app.call(`${SERVICE}/readCredentials`, { accountId: result._id, name: "nope" }),
            failsWith({ errorCode: -9999, errorText: "MojoService: no errorCode supplied Credentials not found: nope" }));
    });

    test("a stranger may not read or write them", async () => {
        const { result } = await created();
        await assert.rejects(env.stranger.call(`${SERVICE}/readCredentials`, { accountId: result._id, name: "token" }),
            failsWith({ errorText: /Permission denied/ }));
        await assert.rejects(env.stranger.call(`${SERVICE}/writeCredentials`, { accountId: result._id, name: "t", credentials: {} }),
            failsWith({ errorText: /Permission denied/ }));
    });
});

describe("modifyAccount", () => {
    const modify = (accountId: unknown, object: Payload, caller: Bus = env.app, extra: Payload = {}) =>
        caller.call(`${SERVICE}/modifyAccount`, { accountId, object, ...extra });

    test("changes the alias and keeps the rest", async () => {
        const { result } = await created();
        await modify(result._id, { alias: "Renamed" });
        const stored = await accountIn(String(result._id));
        assert.equal(stored!.alias, "Renamed");
        assert.equal(stored!.username, "me@test");
        assert.equal((stored!.capabilityProviders as Payload[]).length, 2);
    });

    test("enabling and disabling providers calls onCapabilitiesChanged and onEnabled for each", async () => {
        const { result } = await created();
        env.calls.length = 0;
        await modify(result._id, { capabilityProviders: [{ id: `${MAIL}.contacts` }, { id: `${MAIL}.calendar` }] });
        const stored = await accountIn(String(result._id));
        assert.deepEqual((stored!.capabilityProviders as Payload[]).map((p) => p.id), [`${MAIL}.contacts`, `${MAIL}.calendar`]);
        assert.deepEqual(calledWith("capabilitiesChanged", result._id)[0]!.capabilityProviders, [
            { id: `${MAIL}.mail`, enabled: false }, { id: `${MAIL}.contacts`, enabled: true },
            { id: `${MAIL}.calendar`, enabled: true },
        ]);
        const enabled = calledWith("enabled", result._id).map((p) => [p.capabilityProviderId, p.enabled]).sort();
        assert.deepEqual(enabled, [[`${MAIL}.calendar`, true], [`${MAIL}.mail`, false]]);
    });

    test("an alwaysOn provider cannot be disabled", async () => {
        const { result } = await created();
        await assert.rejects(modify(result._id, { capabilityProviders: [{ id: `${MAIL}.mail` }] }),
            failsWith({ errorCode: "400_BAD_REQUEST", errorText: "can't disable 'alwaysOn' capabilities" }));
    });

    test("new credentials replace the old ones, clear the status and tell the transports", async () => {
        const { result } = await created();
        // Transports always name themselves; db8 does not index a status without it.
        await env.tempdb.put([{ _kind: "com.palm.account.syncstate:1", accountId: result._id,
            capabilityProvider: `${MAIL}.mail`, syncState: "ERROR" }]);
        env.calls.length = 0;
        await modify(result._id, { credentials: { fresh: "yes" } });
        await assert.rejects(env.app.call(`${SERVICE}/readCredentials`, { accountId: result._id, name: "token" }));
        assert.equal((await env.app.call(`${SERVICE}/readCredentials`, { accountId: result._id, name: "fresh" })).credentials, "yes");
        assert.deepEqual((await env.tempdb.find({ from: "com.palm.account.syncstate:1" })).results, []);
        assert.deepEqual(calledWith("credentialsChanged", result._id), [{ accountId: result._id }]);
        assert.deepEqual(calledWith("mailCredentialsChanged", result._id), [{ accountId: result._id }]);
    });

    test("suppressNotifications keeps the transports out of it", async () => {
        const { result } = await created();
        env.calls.length = 0;
        await modify(result._id, { credentials: { fresh: "yes" } }, env.app, { suppressNotifications: true });
        assert.deepEqual(calledWith("credentialsChanged", result._id), []);
    });

    test("a transport that fails undoes its provider's change", async () => {
        const { result } = await created();
        env.failing.add("enabled");
        await assert.rejects(modify(result._id, {
            capabilityProviders: [{ id: `${MAIL}.mail` }, { id: `${MAIL}.contacts` }, { id: `${MAIL}.calendar` }],
        }), failsWith({ errorCode: "TRANSPORT_FAILURE", errorText: `Failed to notify transports: ${T}/enabled` }));
        const stored = await accountIn(String(result._id));
        assert.deepEqual((stored!.capabilityProviders as Payload[]).map((p) => p.id), [`${MAIL}.mail`, `${MAIL}.contacts`]);
    });

    test("new credentials alone, with a transport down, leave the providers as they were", async () => {
        const { result } = await created();
        env.failing.add("credentialsChanged");
        await assert.rejects(modify(result._id, { credentials: { fresh: "yes" } }),
            failsWith({ errorCode: "TRANSPORT_FAILURE" }));
        const stored = await accountIn(String(result._id));
        assert.deepEqual((stored!.capabilityProviders as Payload[]).map((p) => p.id), [`${MAIL}.mail`, `${MAIL}.contacts`]);
    });

    test("an account whose template is gone: the accounts app may still rename it, others may not", async () => {
        const [orphan] = await env.db.put([{ _kind: "com.palm.account:1", templateId: "com.webosce.test.gone",
            username: "left", beingDeleted: false, capabilityProviders: [] }]);
        await modify(orphan!.id, { alias: "Kept" }, env.accountsApp);
        assert.equal((await accountIn(orphan!.id))!.alias, "Kept");
        await assert.rejects(modify(orphan!.id, { alias: "No" }, env.app), failsWith({ errorCode: -9999 }));
    });

    test("a stranger may not modify", async () => {
        const { result } = await created();
        await assert.rejects(modify(result._id, { alias: "x" }, env.stranger), failsWith({ errorText: /Permission denied/ }));
    });
});

describe("deleting", () => {
    test("deleteAccount marks the account, shows the status, and the transports finish it", async () => {
        const { result } = await created();
        const accountId = String(result._id);
        const release: { go?: () => void } = {};
        env.holding.set("deleted", new Promise((resolve) => { release.go = resolve; }));
        await env.app.call(`${SERVICE}/deleteAccount`, { accountId });
        // While the transport works, the account shows as being removed.
        assert.equal((await accountIn(accountId))!.beingDeleted, true);
        const status = await env.tempdb.find({ from: "com.palm.account.syncstate:1" });
        assert.deepEqual(status.results.map((s) => [s.accountId, s.syncState, s.capabilityProvider]),
            [[accountId, "DELETE", "com.palm.service.accounts"]]);
        await until(() => calledWith("deleted", accountId).length === 1, "onDelete");
        release.go!();
        await until(async () => (await env.db.get([accountId]))[0] === undefined
            || (await env.db.get([accountId]))[0]!._del === true, "the account to be deleted");
        assert.deepEqual(calledWith("deleted", accountId), [{ accountId }]);
        assert.equal((await env.app.call(`${SERVICE}/hasCredentials`, { accountId })).value, false);
        assert.deepEqual((await env.tempdb.find({ from: "com.palm.account.syncstate:1" })).results, []);
    });

    test("an account being deleted, or gone, cannot be deleted again", async () => {
        const { result } = await created();
        await env.db.merge([{ _id: String(result._id), beingDeleted: true }]);
        await assert.rejects(env.app.call(`${SERVICE}/deleteAccount`, { accountId: result._id }),
            failsWith({ errorText: "MojoService: no errorCode supplied Account is being deleted" }));
        await assert.rejects(env.app.call(`${SERVICE}/deleteAccount`, { accountId: "nope" }),
            failsWith({ errorText: "MojoService: no errorCode supplied Unable to find account" }));
    });

    test("a transport refusing the deletion leaves the account marked", async () => {
        const { result } = await created();
        env.failing.add("deleted");
        await env.db.merge([{ _id: String(result._id), beingDeleted: true }]);
        await assert.rejects(env.app.call(`${SERVICE}/notifyAccountDeleted`, { accountId: result._id }),
            failsWith({ errorCode: "TRANSPORT_FAILURE" }));
        assert.equal((await accountIn(String(result._id)))!.beingDeleted, true);
        assert.equal((await env.app.call(`${SERVICE}/hasCredentials`, { accountId: result._id })).value, true);
    });

    test("stayRunning waits, then retries interrupted deletions up to five times", async () => {
        const { result } = await created();
        const accountId = String(result._id);
        env.failing.add("deleted");
        await env.db.merge([{ _id: accountId, beingDeleted: true, retries: 3 }]);
        await env.app.call(`${SERVICE}/stayRunning`, { seconds: "0.05" });
        await until(async () => (await accountIn(accountId))!.retries === 4, "a retry");
        await until(() => calledWith("deleted", accountId).length === 1, "the retried deletion");
        await env.db.merge([{ _id: accountId, retries: 5 }]);
        env.calls.length = 0;
        await env.app.call(`${SERVICE}/stayRunning`, { seconds: "0.05" });
        await sleep(200);
        assert.equal((await accountIn(accountId))!.retries, 5);
        assert.deepEqual(calledWith("deleted", accountId), []);
    });
});

describe("restore and upkeep", () => {
    test("restoreCompleted restarts the transports, cleans up, and names the profile", async () => {
        const { result: kept } = await created();
        const { result: dying } = await created({ username: "dying@test" });
        await env.db.merge([{ _id: String(dying._id), beingDeleted: true }]);
        const profile = await env.app.call<{ accountCreated: boolean }>(`${SERVICE}/createLocalAccount`);
        assert.equal(profile.accountCreated, true);
        await until(async () => (await env.db.find(PROFILE_QUERY)).results.length === 1, "the profile account");
        env.calls.length = 0;

        await env.app.call(`${SERVICE}/accountsChanged`);
        assert.deepEqual(calledWith("capabilitiesChanged").map((p) => p.accountId), [kept._id]);
        await until(async () => (await env.db.get([String(dying._id)]))[0]?._del === true
            || (await env.db.get([String(dying._id)]))[0] === undefined, "the dying account to go");
        await until(async () => (await env.db.find(PROFILE_QUERY)).results[0]?.deviceName === "Test Pad",
        "the device name");
        const [stored] = (await env.db.find(PROFILE_QUERY)).results;
        await until(async () => ((await accountIn(String(stored!._id)))!.capabilityProviders as Payload[]).length === 7,
            "every profile capability enabled");
    });

    test("createLocalAccount makes the profile only once, and leaves the flag", async () => {
        const flagged = env.flagged;
        assert.equal((await env.app.call(`${SERVICE}/createLocalAccount`)).accountCreated, true);
        await until(() => env.flagged === flagged + 1, "the flag");
        await until(async () => (await env.app.call<{ results: Payload[] }>(`${SERVICE}/listAccounts`,
            { templateId: "com.palm.palmprofile" })).results.length === 1, "the profile account");
        assert.equal((await env.app.call(`${SERVICE}/createLocalAccount`)).accountCreated, false);
    });

    test("a flag that cannot be written is only logged", async () => {
        const unhandled: unknown[] = [];
        const record = (reason: unknown) => unhandled.push(reason);
        process.on("unhandledRejection", record);
        env.flagFails = true;
        try {
            assert.equal((await env.app.call(`${SERVICE}/createLocalAccount`)).accountCreated, true);
            await until(async () => (await env.app.call<{ results: Payload[] }>(`${SERVICE}/listAccounts`,
                { templateId: "com.palm.palmprofile" })).results.length === 1, "the profile account");
            await sleep(100);
            assert.deepEqual(unhandled, []);
            assert.deepEqual((await env.app.call(`${SERVICE}/listAccountTemplates`)).returnValue, true);
        } finally {
            env.flagFails = false;
            process.off("unhandledRejection", record);
        }
    });

    test("setPrefs saves the device name on the profile account", async () => {
        await env.app.call(`${SERVICE}/createLocalAccount`);
        await until(async () => (await env.app.call<{ results: Payload[] }>(`${SERVICE}/listAccounts`,
            { templateId: "com.palm.palmprofile" })).results.length === 1, "the profile account");
        await env.app.call(`${SERVICE}/setPrefs`, { deviceName: "Renamed Pad" });
        await until(async () => (await env.app.call<{ results: Payload[] }>(`${SERVICE}/listAccounts`,
            { templateId: "com.palm.palmprofile" })).results[0]?.deviceName === "Renamed Pad", "the new name");
    });

    test("the public bus answers the public commands, and only those", async () => {
        const outsider = openBus(null, { public: true });
        try {
            const { results } = await outsider.call<{ results: Payload[] }>(`${SERVICE}/listAccountTemplates`);
            assert.equal(results.length >= 2, true);
            await assert.rejects(outsider.call(`${SERVICE}/listAccounts`), isLunaError);
        } finally {
            outsider.close();
        }
    });

    test("a new locale restarts the service; __quit exits", async () => {
        const exits = env.exits;
        env.locale = { languageCode: "es", countryCode: "mx" };
        env.pushLocale!();
        await until(() => env.exits === exits + 1, "the restart");
        await env.app.call(`${SERVICE}/__quit`);
        await until(() => env.exits === exits + 2, "__quit");
    });
});
