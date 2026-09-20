// Software Manager against a fake applicationManager / appinstaller.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createFakeLuna, type FakeLuna } from "@webos/api/infra/luna/fake.service.ts";
import { createSwManagerService } from "../src/swmanager.service.ts";
import { appOf } from "../src/luna/swmanager.ts";
import type { Payload } from "@webos/api/infra/luna/service.ts";

const APPS = "luna://com.palm.applicationManager/";
const INSTALLER = "luna://com.palm.appinstaller/";
const LIST = `${APPS}listApps`;
const REMOVE = `${INSTALLER}remove`;

const settle = () => new Promise((resolve) => setImmediate(resolve));

const payloads = (luna: FakeLuna, uri: string): Payload[] =>
    luna.calls.filter((call) => call.uri === uri).map((call) => call.payload);

const setup = () => {
    const luna = createFakeLuna();
    luna.answer(LIST, () => ({
        returnValue: true,
        apps: [
            {
                id: "com.example.removable",
                title: "Notes",
                version: "1.0.0",
                vendor: "Example",
                removable: true,
            },
            {
                id: "com.palm.app.browser",
                title: "Web",
                version: "3.0.0",
                vendor: "HP",
                removable: false,
            },
        ],
    }));
    luna.answer(REMOVE, () => ({ returnValue: true }));
    const service = createSwManagerService(luna);
    return { luna, service, data: () => service.getState().data };
};

describe("appOf", () => {
    test("treats missing removable as true; string false as not removable", () => {
        assert.equal(appOf({ id: "a", title: "A" }).removable, true);
        assert.equal(appOf({ id: "b", title: "B", removable: "false" }).removable, false);
        assert.equal(appOf({ id: "c", title: "C", removable: false }).removable, false);
    });
});

describe("installed apps", () => {
    test("lists apps sorted by title on shown", async () => {
        const { service, data } = setup();
        service.onShown();
        await settle();
        assert.equal(data().apps.length, 2);
        assert.equal(data().apps[0]?.title, "Notes");
        assert.equal(data().apps[1]?.title, "Web");
    });

    test("details for a removable app can delete via appinstaller/remove", async () => {
        const { luna, service, data } = setup();
        service.onShown();
        await settle();
        service.onOpenDetails("com.example.removable");
        assert.equal(data().screen, "details");
        service.onAskDelete();
        assert.equal(data().confirmDelete, true);
        service.onConfirmDelete("ok");
        await settle();
        assert.equal(payloads(luna, REMOVE)[0]?.id, "com.example.removable");
        assert.equal(data().screen, "list");
    });

    test("non-removable apps refuse delete without calling remove", async () => {
        const { luna, service, data } = setup();
        service.onShown();
        await settle();
        service.onOpenDetails("com.palm.app.browser");
        service.onAskDelete();
        assert.match(data().message, /cannot be deleted/i);
        assert.equal(data().confirmDelete, undefined);
        assert.equal(payloads(luna, REMOVE).length, 0);
    });
});
