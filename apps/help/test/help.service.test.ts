// Help TOC against a fake applicationManager.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createFakeLuna, type FakeLuna } from "@webos/api/infra/luna/fake.service.ts";
import { createHelpService } from "../src/help.service.ts";
import { HELP_BASE, HELP_TOPICS, topicUrl } from "../src/luna/help.ts";
import type { Payload } from "@webos/api/infra/luna/service.ts";

const OPEN = "luna://com.palm.applicationManager/open";

const settle = () => new Promise((resolve) => setImmediate(resolve));

const payloads = (luna: FakeLuna, uri: string): Payload[] =>
    luna.calls.filter((call) => call.uri === uri).map((call) => call.payload);

describe("HELP_TOPICS / topicUrl", () => {
    test("ships Archive topics and builds URLs", () => {
        assert.ok(HELP_TOPICS.length >= 4);
        assert.ok(HELP_TOPICS.every((t) => topicUrl(t).startsWith("https://help.webosarchive.org")));
        const clips = HELP_TOPICS.find((t) => t.id === "clips");
        assert.ok(clips);
        assert.ok(topicUrl(clips).includes("clips"));
        assert.equal(topicUrl(HELP_TOPICS[0]!), HELP_BASE);
        assert.notEqual(topicUrl(clips), HELP_BASE);
    });
});

describe("opening topics", () => {
    test("opens a topic URL via applicationManager", async () => {
        const luna = createFakeLuna();
        luna.answer(OPEN, () => ({ returnValue: true }));
        const service = createHelpService(luna);
        service.onShown();
        service.onOpenTopic("tips");
        await settle();
        assert.equal(payloads(luna, OPEN)[0]?.target, HELP_BASE);
    });

    test("unknown topic does not open", async () => {
        const luna = createFakeLuna();
        luna.answer(OPEN, () => ({ returnValue: true }));
        const service = createHelpService(luna);
        service.onOpenTopic("missing");
        await settle();
        assert.equal(payloads(luna, OPEN).length, 0);
    });
});
