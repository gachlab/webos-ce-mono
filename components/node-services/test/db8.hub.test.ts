// kit/db8.ts against a real mojodb-luna on a private hub.

import assert from "node:assert/strict";
import { after, before, describe, test } from "node:test";

import { createDb8, type Db8, type DbObject } from "#kit/db8.ts";
import { isLunaError, openBus, type Bus } from "#kit/luna.ts";
import { startTestBus, type TestBus } from "./hub.ts";

const CLIENT = "com.webosce.test.db8client";
const KIND = `${CLIENT}.item:1`;

interface Item extends DbObject {
    name: string;
    n: number;
}

const env: { hub?: TestBus; bus?: Bus; db?: Db8 } = {};
const db = () => env.db!;

before(async () => {
    env.hub = await startTestBus({ services: [], db8: true });
    env.bus = openBus(CLIENT);
    env.db = createDb8(env.bus);
    await env.hub.startDb8(async () => {
        try {
            await env.bus!.call("luna://com.palm.db/reserveIds", { count: 1 }, { timeout: 1 });
            return true;
        } catch {
            return false;
        }
    });
    await db().putKind({
        id: KIND,
        owner: CLIENT,
        indexes: [{ name: "n", props: [{ name: "n" }] }, { name: "name", props: [{ name: "name" }] }],
    });
});

after(() => {
    env.bus?.close();
    env.hub?.stop();
});

const items = (...ns: number[]): Item[] => ns.map((n) => ({ _kind: KIND, name: `item ${n}`, n }));

const clear = async () => {
    await db().delWhere({ from: KIND }, { purge: true });
};

describe("objects", () => {
    test("put returns ids and revisions, and get reads the objects back", async () => {
        await clear();
        const written = await db().put(items(1, 2));
        assert.equal(written.length, 2);
        assert.ok(written.every((result) => typeof result.id === "string" && typeof result.rev === "number"));
        const read = await db().get<Item>(written.map((result) => result.id));
        assert.deepEqual(read.map((item) => item.name), ["item 1", "item 2"]);
        assert.equal(read[0]!._id, written[0]!.id);
    });

    test("find filters and orders, without returnValue in the reply", async () => {
        await clear();
        await db().put(items(3, 1, 2));
        const reply = await db().find<Item>({ from: KIND, where: [{ prop: "n", op: ">", val: 1 }], orderBy: "n" });
        assert.deepEqual(reply.results.map((item) => item.n), [2, 3]);
        assert.equal("returnValue" in reply, false);
        assert.equal(reply.next, undefined);
    });

    test("find can count", async () => {
        await clear();
        await db().put(items(1, 2, 3));
        const reply = await db().find({ from: KIND, limit: 1 }, { count: true });
        assert.equal(reply.count, 3);
        assert.equal(reply.results.length, 1);
        assert.equal(typeof reply.next, "string");
    });

    test("findAll walks every page", async () => {
        await clear();
        await db().put(items(1, 2, 3, 4, 5));
        const seen: number[] = [];
        for await (const item of db().findAll<Item>({ from: KIND, orderBy: "n", limit: 2 })) {
            seen.push(item.n);
        }
        assert.deepEqual(seen, [1, 2, 3, 4, 5]);
    });

    test("findAll stops fetching when the loop stops", async () => {
        await clear();
        await db().put(items(1, 2, 3, 4, 5));
        const seen: number[] = [];
        for await (const item of db().findAll<Item>({ from: KIND, orderBy: "n", limit: 2 })) {
            seen.push(item.n);
            if (seen.length === 3) {
                break;
            }
        }
        assert.deepEqual(seen, [1, 2, 3]);
    });

    test("merge changes the named properties only", async () => {
        await clear();
        const [written] = await db().put(items(1));
        await db().merge([{ _id: written!.id, name: "renamed" }]);
        const [read] = await db().get<Item>([written!.id]);
        assert.equal(read!.name, "renamed");
        assert.equal(read!.n, 1);
    });

    test("mergeWhere counts what it changed", async () => {
        await clear();
        await db().put(items(1, 2, 3));
        const count = await db().mergeWhere({ from: KIND, where: [{ prop: "n", op: "<", val: 3 }] }, { name: "small" });
        assert.equal(count, 2);
        const { results } = await db().find<Item>({ from: KIND, where: [{ prop: "name", op: "=", val: "small" }] });
        assert.equal(results.length, 2);
    });

    test("del marks objects deleted, and purge removes them", async () => {
        await clear();
        const written = await db().put(items(1, 2));
        await db().del([written[0]!.id]);
        // db8 cannot order by an index while including deleted objects.
        const byN = (results: Item[]) => [...results].sort((x, y) => x.n - y.n);
        const kept = await db().find<Item>({ from: KIND, incDel: true });
        assert.deepEqual(byN(kept.results).map((item) => item._del ?? false), [true, false]);
        await db().del([written[1]!.id], { purge: true });
        const left = await db().find<Item>({ from: KIND, incDel: true });
        assert.deepEqual(byN(left.results).map((item) => item.n), [1]);
    });

    test("delWhere counts what it removed", async () => {
        await clear();
        await db().put(items(1, 2, 3));
        assert.equal(await db().delWhere({ from: KIND, where: [{ prop: "n", op: ">=", val: 2 }] }), 2);
        assert.equal((await db().find({ from: KIND })).results.length, 1);
    });

    test("batch returns one response per operation", async () => {
        await clear();
        const responses = await db().batch([
            { method: "put", params: { objects: items(7) } },
            { method: "find", params: { query: { from: KIND } } },
        ]);
        assert.equal(responses.length, 2);
        assert.equal((responses[1]!.results as Item[])[0]!.n, 7);
    });

    test("db8's own errors arrive as LunaErrors with db8's code", async () => {
        await assert.rejects(db().find({ from: "com.webosce.test.nokind:1" }), (error: unknown) => {
            assert.ok(isLunaError(error));
            assert.equal(error.errorCode, -3970);
            return true;
        });
    });
});

describe("watchFind", () => {
    test("yields the results now, then again after each change", async () => {
        await clear();
        await db().put(items(1));
        const abort = new AbortController();
        const sets = db().watchFind<Item>({ from: KIND, orderBy: "n" }, { signal: abort.signal });
        assert.deepEqual((await sets.next()).value!.map((item) => item.n), [1]);
        await db().put(items(2));
        assert.deepEqual((await sets.next()).value!.map((item) => item.n), [1, 2]);
        await db().put(items(3));
        assert.deepEqual((await sets.next()).value!.map((item) => item.n), [1, 2, 3]);
        abort.abort();
        assert.equal((await sets.next()).done, true);
    });

    test("aborting while it waits for a change ends it", async () => {
        await clear();
        const abort = new AbortController();
        const sets = db().watchFind({ from: KIND }, { signal: abort.signal });
        await sets.next();
        const waiting = sets.next();
        abort.abort();
        assert.equal((await waiting).done, true);
    });

    test("leaving the loop ends it", async () => {
        await clear();
        const seen: number[] = [];
        for await (const results of db().watchFind({ from: KIND })) {
            seen.push(results.length);
            break;
        }
        assert.deepEqual(seen, [0]);
    });
});
