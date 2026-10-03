// The Print Manager card's state machine, against a fake com.palm.printmgr.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createFakeLuna, type FakeLuna } from "@webos/api/infra/luna/fake.service.ts";
import { createPrintService } from "../src/printmanager.service.ts";
import type { Payload } from "@webos/api/infra/luna/service.ts";

const PRINTERS = "luna://com.palm.printmgr/printers/";
const JOBS = "luna://com.palm.printmgr/jobs/";
const LIST_SUB = `${PRINTERS}list`;

const settle = () => new Promise((resolve) => setImmediate(resolve));

const printer = (id: string, name: string, address = "ipp://host/printers/" + id) => ({
    eventType: "Add",
    printerID: id,
    printerName: name,
    printerAddress: address,
});

const setup = (current = "") => {
    const luna = createFakeLuna();
    luna.answer(LIST_SUB, () => ({ returnValue: true }));
    luna.answer(`${PRINTERS}getCurrent`, () =>
        current ? { returnValue: true, printerID: current, printerName: current, printerAddress: "" }
                : { returnValue: true });
    luna.answer(`${PRINTERS}setCurrent`, () => ({ returnValue: true }));
    luna.answer(`${JOBS}cancel`, () => ({ returnValue: true }));
    const service = createPrintService(luna);
    return { luna, service, data: () => service.getState().data };
};

const pushTo = (luna: FakeLuna, uri: string, reply: Payload) => {
    const subs = luna.subscribers.filter((one) => one.uri === uri);
    assert.ok(subs.length > 0, `the card is watching ${uri}`);
    for (const sub of subs)
        sub.push({ returnValue: true, ...reply });
};

const payloads = (luna: FakeLuna, uri: string): Payload[] =>
    luna.calls.filter((call) => call.uri === uri).map((call) => call.payload);

describe("the printer list", () => {
    test("subscribes to printers/list and folds Add events into the list", async () => {
        const { luna, service, data } = setup();
        service.onShown();
        await settle();

        assert.ok(luna.subscribers.some((s) => s.uri === LIST_SUB), "watches printers/list");
        assert.equal(data().searching, true);

        pushTo(luna, LIST_SUB, printer("hp", "HP LaserJet"));
        pushTo(luna, LIST_SUB, printer("epson", "Epson WF"));
        assert.equal(data().printers.length, 2);
        assert.equal(data().searching, false, "a printer arriving ends the searching state");
        assert.deepEqual(data().printers.map((p) => p.name), ["HP LaserJet", "Epson WF"]);
    });

    test("a Rmv event drops the printer; an Add for a known id updates in place", async () => {
        const { luna, service, data } = setup();
        service.onShown();
        await settle();

        pushTo(luna, LIST_SUB, printer("hp", "HP LaserJet"));
        pushTo(luna, LIST_SUB, printer("epson", "Epson WF"));
        // Update in place: same id, new name.
        pushTo(luna, LIST_SUB, printer("hp", "HP LaserJet Pro"));
        assert.equal(data().printers.length, 2, "an Add for a known id does not duplicate");
        assert.equal(data().printers.find((p) => p.id === "hp")?.name, "HP LaserJet Pro");

        pushTo(luna, LIST_SUB, { eventType: "Rmv", printerID: "epson" });
        assert.equal(data().printers.length, 1);
        assert.equal(data().printers[0]?.id, "hp");
    });

    test("a list event with no printerID is ignored", async () => {
        const { luna, service, data } = setup();
        service.onShown();
        await settle();
        pushTo(luna, LIST_SUB, { eventType: "Add", printerName: "nameless" });
        assert.equal(data().printers.length, 0);
    });
});

describe("the current printer", () => {
    test("getCurrent marks the default on show", async () => {
        const { service, data } = setup("hp");
        service.onShown();
        await settle();
        assert.equal(data().currentId, "hp");
    });

    test("selecting a printer sets it current and calls setCurrent with its id", async () => {
        const { luna, service, data } = setup();
        service.onShown();
        await settle();
        pushTo(luna, LIST_SUB, printer("epson", "Epson WF"));

        service.onSelectPrinter("epson");
        assert.equal(data().currentId, "epson");
        await settle();
        const sent = payloads(luna, `${PRINTERS}setCurrent`);
        assert.equal(sent.length, 1);
        assert.equal(sent[0]?.printerID, "epson");
    });

    test("selecting the already-current printer does nothing", async () => {
        const { luna, service } = setup("hp");
        service.onShown();
        await settle();
        service.onSelectPrinter("hp");
        await settle();
        assert.equal(payloads(luna, `${PRINTERS}setCurrent`).length, 0);
    });

    test("a rejected setCurrent rolls the selection back", async () => {
        const { luna, service, data } = setup("hp");
        luna.answer(`${PRINTERS}setCurrent`, () => {
            throw new Error("refused");
        });
        service.onShown();
        await settle();
        pushTo(luna, LIST_SUB, printer("epson", "Epson WF"));

        service.onSelectPrinter("epson");
        await settle();
        assert.equal(data().currentId, "hp", "the selection rolls back to the previous current");
        assert.ok(data().message.length > 0, "and an error line is shown");
    });
});

describe("lifecycle", () => {
    test("onHidden cancels the subscription so a second show does not stack it", async () => {
        const { luna, service } = setup();
        service.onShown();
        await settle();
        const before = luna.subscribers.filter((s) => s.uri === LIST_SUB).length;
        service.onHidden();
        // A cancelled subscription is removed from subscribers entirely.
        const afterHide = luna.subscribers.filter((s) => s.uri === LIST_SUB).length;
        service.onShown();
        await settle();
        const after = luna.subscribers.filter((s) => s.uri === LIST_SUB).length;
        assert.equal(before, 1);
        assert.equal(afterHide, 0, "hide cancels the subscription");
        assert.equal(after, 1, "exactly one live subscription after hide/show");
    });
});
