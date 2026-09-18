import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createFakeLuna } from "@webos/api/infra/luna/fake.service.ts";
import { createSwManagerService } from "../src/swmanager.service.ts";
import { appOf } from "../src/luna/swmanager.ts";

const settle = () => new Promise((resolve) => setImmediate(resolve));

describe("appOf", () => {
    test("treats missing removable as removable", () => {
        assert.equal(appOf({ id: "a", title: "A" }).removable, true);
        assert.equal(appOf({ id: "b", title: "B", removable: false }).removable, false);
    });
});

describe("software service", () => {
    test("lists apps and removes a removable one", async () => {
        const luna = createFakeLuna();
        luna.answer("luna://com.palm.applicationManager/listApps", () => ({
            returnValue: true,
            apps: [
                { id: "com.gachlab.app.demo", title: "Demo", version: "1.0", removable: true },
                { id: "com.palm.app.browser", title: "Web", version: "1.0", removable: false },
            ],
        }));
        luna.answer("luna://com.palm.appinstaller/remove", () => ({ returnValue: true }));
        const service = createSwManagerService(luna);
        service.onShown();
        await settle();
        assert.equal(service.getState().data.apps.length, 2);
        service.onOpenDetails("com.gachlab.app.demo");
        service.onAskDelete();
        service.onConfirmDelete("ok");
        await settle();
        const remove = luna.calls.find((c) => c.uri === "luna://com.palm.appinstaller/remove");
        assert.ok(remove);
    });
});
