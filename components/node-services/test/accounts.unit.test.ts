// The pure parts of com.palm.service.accounts, and the schema validator.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { validate } from "#kit/json-schema.ts";
import { mojoFailure } from "#kit/mojoservice.ts";
import {
    annotate, diffLists, globToRegex, providerStates, publicAccessByCapability, publicAccessById, requirePermission,
    storedProviders, stripProcessNumber, type Account, type Template,
} from "../services/com.palm.service.accounts/accounts.ts";
import {
    createTemplateLoader, localizedCandidates, parseLocale, type TemplateFiles,
} from "../services/com.palm.service.accounts/templates.ts";

const template: Template = {
    templateId: "com.example.mail",
    loc_name: "Example",
    onCapabilitiesChanged: "luna://com.example/changed",
    readPermissions: ["com.example.*"],
    writePermissions: ["com.example.app"],
    capabilityProviders: [
        { id: "com.example.mail.mail", capability: "MAIL", onEnabled: "luna://com.example/enabled", loc_name: "Mail" },
        { id: "com.example.mail.contacts", capability: "CONTACTS", alwaysOn: true },
    ],
};

const account: Account = {
    _id: "a1",
    templateId: "com.example.mail",
    username: "me",
    loc_name: "stored name",
    extra: "kept",
    capabilityProviders: [{ id: "com.example.mail.mail", capability: "MAIL", _id: "p1", loc_name: "stored" }],
};

describe("json-schema", () => {
    const schema = {
        type: "object",
        properties: {
            name: { type: "string" },
            size: { type: ["integer", "null"], optional: true },
            tags: { type: "array", optional: true, items: { type: "string" } },
            kind: { type: "string", optional: true, enum: ["a", "b"] },
        },
    };

    test("a matching object is valid, and extra properties are allowed", () => {
        assert.deepEqual(validate({ name: "x", size: 3, tags: ["t"], other: 1 }, schema), { valid: true, errors: [] });
    });

    test("a property is required unless it is optional", () => {
        assert.deepEqual(validate({}, schema).errors.map((e) => e.property), ["name"]);
    });

    test("types, unions, items and enums are checked", () => {
        const result = validate({ name: 1, size: 1.5, tags: ["ok", 2], kind: "c" }, schema);
        assert.deepEqual(result.errors.map((e) => e.property), ["name", "size", "tags[1]", "kind"]);
        assert.equal(validate({ name: "x", size: null }, schema).valid, true);
        assert.equal(validate("text", { type: "any" }).valid, true);
        assert.equal(validate([1], { type: "number" }).valid, false);
    });

    test("additionalProperties false or a schema limits extra properties", () => {
        assert.deepEqual(validate({ a: 1 }, { type: "object", additionalProperties: false }).errors.map((e) => e.property), ["a"]);
        assert.equal(validate({ a: 1 }, { type: "object", additionalProperties: { type: "string" } }).valid, false);
        assert.equal(validate({ a: "1" }, { type: "object", additionalProperties: { type: "string" } }).valid, true);
    });

    test("as in Foundations, null and arrays pass as objects", () => {
        assert.equal(validate(null, { type: "object" }).valid, true);
        assert.equal(validate([1], { type: "object" }).valid, true);
        assert.equal(validate(1, { type: "object" }).valid, false);
    });

    test("only a property of the object's own counts", () => {
        assert.deepEqual(validate({}, { type: "object", properties: { constructor: { type: "any" } } }).errors
            .map((e) => e.property), ["constructor"]);
        assert.deepEqual(validate({ toString: 1 }, { type: "object", properties: {}, additionalProperties: false }).errors
            .map((e) => e.property), ["toString"]);
    });

    test("a keyword it does not know is refused", () => {
        assert.throws(() => validate(1, { minimum: 0 } as never), /unsupported schema keyword "minimum"/);
    });

    test("HP's template schemas are within what it knows", async () => {
        const { readFileSync } = await import("node:fs");
        for (const name of ["template.json", "template-file.json"]) {
            const hp = JSON.parse(readFileSync(new URL(`../services/com.palm.service.accounts/schemas/${name}`, import.meta.url), "utf8"));
            assert.doesNotThrow(() => validate([template], hp));
        }
    });
});

describe("mojoservice failures", () => {
    test("an error without a code gets -9999 and mojoservice's prefix", () => {
        const failure = mojoFailure(new Error("Assert.require failed"));
        assert.equal(failure.errorCode, -9999);
        assert.equal(failure.errorText, "MojoService: no errorCode supplied Assert.require failed");
    });

    test("an error with a code keeps it, even an empty one", () => {
        assert.deepEqual([mojoFailure(Object.assign(new Error("no"), { errorCode: "" })).errorCode], [""]);
        assert.equal(mojoFailure({ message: "dup", errorCode: "DUPLICATE_ACCOUNT" }).errorText, "dup");
    });
});

describe("accounts", () => {
    test("annotate: template properties win, the account's extras stay, only its providers are listed", () => {
        const result = annotate(account, [template]);
        assert.equal(result.loc_name, "Example");
        assert.equal(result.extra, "kept");
        assert.equal(result.onCapabilitiesChanged, "luna://com.example/changed");
        assert.deepEqual(result.capabilityProviders, [{
            id: "com.example.mail.mail", capability: "MAIL", _id: "p1", loc_name: "Mail", onEnabled: "luna://com.example/enabled",
        }]);
        assert.equal(account.loc_name, "stored name", "the stored account is not changed");
    });

    test("annotate keeps a provider the template no longer has", () => {
        const odd = { ...account, capabilityProviders: [{ id: "gone" }] };
        assert.deepEqual(annotate(odd, [template]).capabilityProviders, [{ id: "gone" }]);
    });

    test("annotate fails when the template is gone", () => {
        assert.throws(() => annotate({ ...account, templateId: "nope" }, [template]), /template not found: nope/);
    });

    test("providerStates lists every template provider", () => {
        assert.deepEqual(providerStates(template, [{ id: "com.example.mail.contacts" }]), [
            { id: "com.example.mail.mail", enabled: false },
            { id: "com.example.mail.contacts", enabled: true },
        ]);
    });

    test("diffLists", () => {
        assert.deepEqual(diffLists(["a", "b"], ["b", "c"]), { add: ["c"], remove: ["a"] });
    });

    test("storedProviders keeps id and capability, and fails on an unknown provider", () => {
        assert.deepEqual(storedProviders(template, [{ id: "com.example.mail.contacts" }]),
            [{ id: "com.example.mail.contacts", capability: "CONTACTS" }]);
        assert.throws(() => storedProviders(template, [{ id: "nope" }]), TypeError);
    });

    test("globToRegex", () => {
        assert.equal(globToRegex("com.palm.*").test("com.palm.app.x"), true);
        assert.equal(globToRegex("com.palm.*").test("comXpalm.app"), false);
        assert.equal(globToRegex("a?c").test("abc"), true);
        assert.equal(globToRegex("a?c").test("abbc"), false);
        assert.equal(globToRegex("a\\b").test("a\\b"), true);
    });

    test("stripProcessNumber", () => {
        assert.equal(stripProcessNumber("com.app 1234"), "com.app");
        assert.equal(stripProcessNumber("com.app"), "com.app");
    });

    describe("requirePermission", () => {
        const caller = (applicationId: string | undefined, senderServiceName?: string) => ({ applicationId, senderServiceName });

        test("the accounts app and service may always", () => {
            assert.doesNotThrow(() => requirePermission("writePermissions", { ...template, writePermissions: [] },
                caller(undefined, "com.palm.service.accounts")));
            assert.doesNotThrow(() => requirePermission("writePermissions", { ...template, writePermissions: [] },
                caller("com.palm.app.accounts 99")));
        });

        test("a caller matching a glob may; the application id comes first", () => {
            assert.doesNotThrow(() => requirePermission("readPermissions", template, caller("com.example.reader")));
            assert.doesNotThrow(() => requirePermission("readPermissions", template, caller(undefined, "com.example.svc")));
            assert.throws(() => requirePermission("readPermissions", template, caller("com.other", "com.example.svc")));
        });

        test("anyone else is refused with errorCode ''", () => {
            assert.throws(() => requirePermission("writePermissions", template, caller("com.example.reader")), (error: unknown) => {
                const failure = error as { errorCode: unknown; message: string };
                assert.equal(failure.errorCode, "");
                assert.equal(failure.message,
                    "Permission denied! com.example.reader is not specified in template com.example.mail writePermissions");
                return true;
            });
            assert.throws(() => requirePermission("writePermissions", { ...template, writePermissions: undefined },
                caller("com.example.app")));
        });
    });

    test("a missing template: the accounts app may, anyone else fails", () => {
        assert.doesNotThrow(() => requirePermission("writePermissions", undefined,
            { applicationId: "com.palm.app.accounts", senderServiceName: undefined }));
        assert.throws(() => requirePermission("writePermissions", undefined,
            { applicationId: "com.example.app", senderServiceName: undefined }), TypeError);
    });

    test("a caller with no name is refused, even by a template that allows everyone", () => {
        const open = { ...template, readPermissions: ["*"] };
        assert.doesNotThrow(() => requirePermission("readPermissions", open,
            { applicationId: undefined, senderServiceName: "com.anyone" }));
        assert.throws(() => requirePermission("readPermissions", open,
            { applicationId: undefined, senderServiceName: undefined }), /Permission denied! An unnamed caller/);
    });

    test("the public whitelist", () => {
        assert.equal(publicAccessById("com.quickoffice.webos 12"), true);
        assert.equal(publicAccessById("com.other"), false);
        assert.equal(publicAccessByCapability("com.quickoffice.ar", "DOCUMENTS"), true);
        assert.equal(publicAccessByCapability("com.quickoffice.ar", "MAIL"), false);
        assert.equal(publicAccessByCapability("com.quickoffice.ar", undefined), false);
        assert.equal(publicAccessByCapability("com.other", "documents"), false);
    });
});

describe("templates", () => {
    test("parseLocale", () => {
        assert.deepEqual(parseLocale("en_us"), { language: "en", region: "us", locale: "en_us" });
        assert.deepEqual(parseLocale("fr"), { language: "fr", locale: "fr" });
        assert.deepEqual(parseLocale("en_us_att"), { language: "en", region: "us", carrier: "att", locale: "en_us_att" });
    });

    test("the localized lookup order is Globalization.ResourceBundle's", () => {
        assert.deepEqual(localizedCandidates("/t", "a.json", parseLocale("es_mx_tel")), [
            "/t/resources/es/mx/tel/a.json",
            "/t/resources/es/mx/a.json",
            "/t/resources/es/a.json",
            "/t/resources/es_mx_tel/a.json",
            "/t/resources/en/a.json",
            "/t/a.json",
        ]);
    });

    // An in-memory tree: directories end in "/".
    const filesOf = (tree: Record<string, string>): TemplateFiles => ({
        list: (dir) => [...new Set(Object.keys(tree)
            .filter((path) => path.startsWith(`${dir}/`))
            .map((path) => path.slice(dir.length + 1).split("/")[0]!))],
        isDirectory: (path) => Object.keys(tree).some((key) => key.startsWith(`${path}/`)),
        isFile: (path) => path in tree,
        read: (path) => tree[path],
    });

    const schemas = { fileSchema: { type: "array", items: { type: "object" } }, templateSchema: { type: "object", properties: { templateId: { type: "string" } } } };

    const load = (tree: Record<string, string>, locale = "es_mx", logs: string[] = []) =>
        createTemplateLoader({ files: filesOf(tree), roots: ["/r1", "/r2"], ...schemas, log: (m) => logs.push(m) })(locale);

    const json = (value: unknown) => JSON.stringify(value);

    test("reads the localized version, makes icons absolute, and sorts by name", () => {
        const templates = load({
            "/r1/b/b.json": json({ templateId: "b", loc_name: "zeta", capabilityProviders: [] }),
            "/r1/b/resources/es/b.json": json({ templateId: "b", loc_name: "Beta", icon: { loc_32x32: "i.png" },
                capabilityProviders: [{ id: "b.x", icon: { loc_48x48: "x.png" } }] }),
            "/r2/a/a.json": json([{ templateId: "a", loc_name: "alpha", capabilityProviders: [] }]),
            "/r2/a/notes.txt": "ignored",
        });
        assert.deepEqual(templates.map((t) => t.templateId), ["a", "b"]);
        assert.equal(templates[1]!.loc_name, "Beta");
        assert.deepEqual(templates[1]!.icon, { loc_32x32: "/r1/b/i.png" });
        assert.deepEqual(templates[1]!.capabilityProviders[0]!.icon, { loc_48x48: "/r1/b/x.png" });
    });

    test("the first of two templates with the same id wins", () => {
        const logs: string[] = [];
        const templates = load({
            "/r1/one/t.json": json({ templateId: "t", loc_name: "first", capabilityProviders: [] }),
            "/r2/two/t.json": json({ templateId: "t", loc_name: "second", capabilityProviders: [] }),
        }, "en_us", logs);
        assert.deepEqual(templates.map((t) => t.loc_name), ["first"]);
        assert.ok(logs.some((m) => m.includes("duplicate account template: t")));
    });

    test("allowed_locales and disallowed_locales", () => {
        const tree = {
            "/r1/a/a.json": json({ templateId: "a", allowed_locales: ["en_us"], capabilityProviders: [] }),
            "/r1/b/b.json": json({ templateId: "b", disallowed_locales: ["en_us"], capabilityProviders: [] }),
        };
        assert.deepEqual(load(tree, "en_us").map((t) => t.templateId), ["a"]);
        assert.deepEqual(load(tree, "de_de").map((t) => t.templateId), ["b"]);
    });

    test("a broken file is skipped, an invalid template ends its file, and the others still load", () => {
        const logs: string[] = [];
        const templates = load({
            "/r1/bad/bad.json": "{not json",
            // As in HP's loader, what came before the invalid template stays.
            "/r1/invalid/i.json": json([{ templateId: "fine", capabilityProviders: [] }, { templateId: 7 },
                { templateId: "after", capabilityProviders: [] }]),
            "/r1/good/g.json": json({ templateId: "good", capabilityProviders: [] }),
        }, "en_us", logs);
        assert.deepEqual(templates.map((t) => t.templateId).sort(), ["fine", "good"]);
        assert.equal(logs.filter((m) => m.startsWith("Parse error")).length, 2);
    });

    test("roots that do not exist are fine", () => {
        assert.deepEqual(load({}), []);
    });
});
