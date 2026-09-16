// What kit/luna.ts must do, written once and run twice: against the in-memory
// bus (luna.fake.test.ts) and against a real ls-hubd (luna.hub.test.ts).

import assert from "node:assert/strict";
import { after, before, describe, test } from "node:test";
import { setTimeout as sleep } from "node:timers/promises";

import { createActivity, createBus, isLunaError, lunaError, type Bus, type BusOptions, type Payload, type Request } from "#kit/luna.ts";
import type { BusMessage, OpenHandle } from "#kit/handle.ts";

export interface Setup {
    readonly openHandle: OpenHandle;
    readonly teardown: () => void;
}

const SERVICE = "com.webosce.test.service";
const URI = `luna://${SERVICE}`;

const until = async (condition: () => boolean, what: string) => {
    for (let i = 0; i < 100; i++) {
        if (condition()) {
            return;
        }
        await sleep(20);
    }
    assert.fail(`timed out waiting for ${what}`);
};

// Listeners for a raw client handle, which is never called.
const ignore = { onRequest: (_: BusMessage) => {}, onCancel: (_: BusMessage) => {} };

const isLunaErrorWith = (fields: Payload) => (error: unknown) => {
    assert.ok(isLunaError(error), `not a LunaError: ${String(error)}`);
    for (const [key, value] of Object.entries(fields)) {
        assert.deepEqual((error as unknown as Payload)[key], value, key);
    }
    return true;
};

// Every name the suite calls, for a real hub's service files.
export const SERVICES = [SERVICE, "com.webosce.test.idle", "com.webosce.test.closing", "com.webosce.test.failing",
    "com.webosce.test.shared"] as const;

export const lunaSuite = (setUp: () => Promise<Setup>) => {
    // What each handler saw, and what its cleanup did, for the tests to inspect.
    const seen: Request[] = [];
    const cleanedUp: string[] = [];
    const waiting: { release?: (() => void) | undefined } = {};
    const env: { setup?: Setup; service?: Bus; client?: Bus; openBus?: (name: string | null, options?: BusOptions) => Bus } = {};

    const client = () => env.client!;

    before(async () => {
        env.setup = await setUp();
        env.openBus = createBus({
            openHandle: env.setup.openHandle,
            setTimer: (callback, ms) => setTimeout(callback, ms),
            clearTimer: (timer) => clearTimeout(timer as ReturnType<typeof setTimeout> | undefined),
            exit: () => assert.fail("a test bus tried to exit the process"),
        });
        const service = env.openBus(SERVICE);
        env.service = service;
        env.client = env.openBus("com.webosce.test.client");

        service.method<{ a: number; b: number }>("add", (request) => {
            seen.push(request);
            return { sum: request.payload.a + request.payload.b };
        });
        service.method("nothing", () => undefined);
        service.method("refuse", () => {
            throw lunaError("BadThing", "It went wrong", { detail: 7 });
        });
        service.method("crash", () => {
            throw new TypeError("oops");
        });
        service.method("honestFailure", () => ({ returnValue: false, errorCode: 3, errorText: "no" }));
        service.method("slow", () => sleep(3000).then(() => ({ late: true })), { timeout: 0.2 });
        service.method("hang", () => new Promise<Payload>(() => {}));
        service.method("inCategory", () => ({ where: "sub" }), { category: "/sub" });

        service.method("count", async function* (request) {
            seen.push(request);
            try {
                for (let i = 1; i <= 3; i++) {
                    yield { n: i };
                }
            } finally {
                cleanedUp.push("count");
            }
        });
        // Yields once, then waits for something that never comes unless the
        // subscriber cancels: only request.signal can end it.
        service.method("wait", async function* ({ signal }) {
            try {
                yield { waiting: true };
                await new Promise<void>((resolve) => {
                    signal.addEventListener("abort", () => resolve(), { once: true });
                    waiting.release = resolve;
                });
                if (!signal.aborted) {
                    yield { released: true };
                }
            } finally {
                cleanedUp.push("wait");
            }
        });
        service.method("failsFirst", async function* () {
            throw lunaError(41, "never started");
        });
        service.method("streamFails", async function* () {
            yield { first: true };
            throw lunaError(42, "stream broke");
        });
        // Relays another subscription, passing its own signal on.
        service.method("relay", async function* ({ signal }) {
            try {
                for await (const reply of client().subscribe(`${URI}/wait`, {}, { signal })) {
                    yield { relayed: reply };
                }
            } finally {
                cleanedUp.push("relay");
            }
        });
    });

    after(() => {
        env.client?.close();
        env.service?.close();
        env.setup?.teardown();
    });

    describe("calls", () => {
        test("a reply gets returnValue true added", async () => {
            assert.deepEqual(await client().call(`${URI}/add`, { a: 2, b: 3 }), { sum: 5, returnValue: true });
        });

        test("the handler sees the request as it was sent", async () => {
            seen.length = 0;
            await client().call(`${URI}/add`, { a: 1, b: 1 });
            const request = seen[0]!;
            assert.equal(request.method, "add");
            assert.equal(request.category, "/");
            assert.deepEqual(request.payload, { a: 1, b: 1 });
            assert.equal(request.subscribe, false);
            assert.equal(request.senderServiceName, "com.webosce.test.client");
            assert.equal(request.signal.aborted, false);
        });

        test("palm:// works as well as luna://", async () => {
            assert.equal((await client().call(`palm://${SERVICE}/add`, { a: 1, b: 2 })).sum, 3);
        });

        test("a handler that returns nothing still answers", async () => {
            assert.deepEqual(await client().call(`${URI}/nothing`), { returnValue: true });
        });

        test("a method in its own category", async () => {
            assert.deepEqual(await client().call(`${URI}/sub/inCategory`), { where: "sub", returnValue: true });
        });

        test("a LunaError reaches the caller with its code, text and extra fields", async () => {
            await assert.rejects(client().call(`${URI}/refuse`), isLunaErrorWith({
                errorCode: "BadThing",
                errorText: "It went wrong",
                reply: { detail: 7, returnValue: false, errorCode: "BadThing", errorText: "It went wrong" },
            }));
        });

        test("any other exception becomes errorCode -1 with its message", async () => {
            await assert.rejects(client().call(`${URI}/crash`), isLunaErrorWith({ errorCode: -1, errorText: "oops" }));
        });

        test("a handler may answer returnValue false itself", async () => {
            await assert.rejects(client().call(`${URI}/honestFailure`),
                isLunaErrorWith({ errorCode: 3, errorText: "no" }));
        });

        test("a service that does not exist fails through the hub", async () => {
            await assert.rejects(client().call("luna://com.webosce.test.nobody/x"), (error: unknown) => {
                assert.ok(isLunaError(error));
                assert.match(error.errorText, /com\.webosce\.test\.nobody/);
                return true;
            });
        });

        test("a method the service does not have fails", async () => {
            await assert.rejects(client().call(`${URI}/missing`), isLunaError);
        });

        test("a malformed payload is refused", async () => {
            const raw = env.setup!.openHandle(null, false, ignore);
            const reply = await new Promise<string>((resolve) => {
                raw.call(`${URI}/add`, "{not json", true, (message) => resolve(message.payload));
            });
            raw.close();
            const parsed = JSON.parse(reply) as Payload;
            assert.equal(parsed.returnValue, false);
            assert.equal(parsed.errorCode, -1);
            assert.match(String(parsed.errorText), /Malformed JSON/);
        });

        test("a method timeout answers 504", async () => {
            await assert.rejects(client().call(`${URI}/slow`), isLunaErrorWith({ errorCode: 504 }));
        });

        test("a call timeout gives up on its own", async () => {
            const started = Date.now();
            await assert.rejects(client().call(`${URI}/hang`, {}, { timeout: 0.3 }), /Timed out/);
            assert.ok(Date.now() - started < 2000);
        });
    });

    describe("subscriptions", () => {
        test("every yield arrives, in order", async () => {
            const got: Payload[] = [];
            for await (const reply of client().subscribe(`${URI}/count`)) {
                got.push(reply);
                if (got.length === 3) {
                    break;
                }
            }
            assert.deepEqual(got, [1, 2, 3].map((n) => ({ n, returnValue: true })));
        });

        test("the handler knows it was subscribed", async () => {
            seen.length = 0;
            const replies = client().subscribe(`${URI}/count`);
            await replies.next();
            await replies.return!();
            assert.equal(seen[0]!.subscribe, true);
        });

        test("a plain call to a generator gets the first yield, and the generator is closed", async () => {
            cleanedUp.length = 0;
            assert.deepEqual(await client().call(`${URI}/count`), { n: 1, returnValue: true });
            await until(() => cleanedUp.includes("count"), "count's finally");
        });

        test("pushes after the first one keep coming while subscribed", async () => {
            waiting.release = undefined;
            const replies = client().subscribe(`${URI}/wait`);
            assert.deepEqual((await replies.next()).value, { waiting: true, returnValue: true });
            await until(() => waiting.release !== undefined, "the handler to wait");
            waiting.release!();
            assert.deepEqual((await replies.next()).value, { released: true, returnValue: true });
            await replies.return!();
        });

        test("a subscriber leaving aborts the handler's signal and runs its finally", async () => {
            cleanedUp.length = 0;
            waiting.release = undefined;
            const replies = client().subscribe(`${URI}/wait`);
            await replies.next();
            await until(() => waiting.release !== undefined, "the handler to wait");
            await replies.return!();
            await until(() => cleanedUp.includes("wait"), "wait's finally");
        });

        test("a generator that fails before its first yield answers with the error", async () => {
            await assert.rejects(client().subscribe(`${URI}/failsFirst`).next(),
                isLunaErrorWith({ errorCode: 41, errorText: "never started" }));
        });

        test("a failure after the first reply is thrown by the iterator", async () => {
            const replies = client().subscribe(`${URI}/streamFails`);
            assert.deepEqual((await replies.next()).value, { first: true, returnValue: true });
            await assert.rejects(replies.next(), isLunaErrorWith({ errorCode: 42, errorText: "stream broke" }));
        });

        test("subscribing to a service that does not exist throws", async () => {
            await assert.rejects(client().subscribe("luna://com.webosce.test.nobody/x").next(), isLunaError);
        });

        test("aborting the signal ends a loop that is waiting", async () => {
            const abort = new AbortController();
            const replies = client().subscribe(`${URI}/wait`, {}, { signal: abort.signal });
            await replies.next();
            const pending = replies.next();
            abort.abort();
            assert.deepEqual(await pending, { value: undefined, done: true });
        });

        test("an already aborted signal yields nothing", async () => {
            const abort = new AbortController();
            abort.abort();
            assert.equal((await client().subscribe(`${URI}/count`, {}, { signal: abort.signal }).next()).done, true);
        });

        test("a relaying handler ends its inner subscription when its own subscriber leaves", async () => {
            cleanedUp.length = 0;
            const replies = client().subscribe(`${URI}/relay`);
            assert.deepEqual((await replies.next()).value,
                { relayed: { waiting: true, returnValue: true }, returnValue: true });
            await replies.return!();
            await until(() => cleanedUp.includes("relay") && cleanedUp.includes("wait"), "both finally blocks");
        });
    });

    describe("idle exit", () => {
        test("fires once nothing is asked for a while, and not during a request or subscription", async () => {
            const name = "com.webosce.test.idle";
            const idle = env.openBus!(name);
            const fired = { count: 0 };
            const held: { finish?: (() => void) | undefined } = {};
            idle.method("hold", () => new Promise<Payload>((resolve) => { held.finish = () => resolve({}); }));
            idle.method("watch", async function* ({ signal }) {
                yield {};
                await new Promise((resolve) => signal.addEventListener("abort", resolve, { once: true }));
            });
            idle.exitWhenIdle(200, () => fired.count++);
            try {
                const call = client().call(`luna://${name}/hold`);
                await until(() => held.finish !== undefined, "the request to arrive");
                await sleep(400);
                assert.equal(fired.count, 0, "fired while a request was open");
                held.finish!();
                await call;

                const watching = client().subscribe(`luna://${name}/watch`);
                await watching.next();
                await sleep(400);
                assert.equal(fired.count, 0, "fired while a subscription was open");
                await watching.return!();

                await until(() => fired.count === 1, "the idle callback");
            } finally {
                idle.close();
            }
        });

        test("a subscription whose generator failed at once does not keep the service up", async () => {
            const name = "com.webosce.test.failing";
            const failing = env.openBus!(name);
            const fired = { count: 0 };
            failing.method("watch", async function* () {
                throw lunaError(5, "no");
            });
            // A raw client that never cancels, unlike luna.ts's own subscribe,
            // so nothing but the service can forget the failed subscription.
            const raw = env.setup!.openHandle(null, false, ignore);
            try {
                const reply = await new Promise<string>((resolve) => {
                    raw.call(`luna://${name}/watch`, JSON.stringify({ subscribe: true }), false,
                        (message) => resolve(message.payload));
                });
                assert.equal((JSON.parse(reply) as Payload).errorCode, 5);
                failing.exitWhenIdle(100, () => fired.count++);
                await until(() => fired.count === 1, "the idle callback");
            } finally {
                raw.close();
                failing.close();
            }
        });

        test("two buses sharing an activity stay up while either is busy", async () => {
            const name = "com.webosce.test.shared";
            const timers = {
                setTimer: (callback: () => void, ms: number) => setTimeout(callback, ms),
                clearTimer: (timer: unknown) => clearTimeout(timer as ReturnType<typeof setTimeout> | undefined),
            };
            const activity = createActivity(timers);
            const quiet = env.openBus!(null, { activity });
            const busy = env.openBus!(name, { public: true, activity });
            const fired = { count: 0 };
            const held: { finish?: (() => void) | undefined } = {};
            busy.method("hold", () => new Promise<Payload>((resolve) => { held.finish = () => resolve({}); }));
            quiet.exitWhenIdle(200, () => fired.count++);
            try {
                // The busy bus is on the public side; the quiet one is private.
                const publicClient = env.openBus!(null, { public: true });
                const publicCall = publicClient.call(`luna://${name}/hold`);
                await until(() => held.finish !== undefined, "the request to arrive");
                await sleep(400);
                assert.equal(fired.count, 0, "the quiet bus fired while the other was busy");
                held.finish!();
                await publicCall;
                publicClient.close();
                await until(() => fired.count === 1, "the idle callback");
            } finally {
                quiet.close();
                busy.close();
                activity.stop();
            }
        });

        test("closing a bus stops its idle timer and ends its subscriptions", async () => {
            const name = "com.webosce.test.closing";
            const closing = env.openBus!(name);
            const fired = { count: 0, ended: false };
            closing.method("watch", async function* ({ signal }) {
                try {
                    yield {};
                    await new Promise((resolve) => signal.addEventListener("abort", resolve, { once: true }));
                } finally {
                    fired.ended = true;
                }
            });
            const watching = client().subscribe(`luna://${name}/watch`);
            await watching.next();
            closing.exitWhenIdle(100, () => fired.count++);
            closing.close();
            await until(() => fired.ended, "the subscription to end");
            await sleep(300);
            assert.equal(fired.count, 0);
            await watching.return!();
        });
    });
};
