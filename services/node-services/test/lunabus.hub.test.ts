// What native/lunabus.cpp must get right beyond what luna-suite.ts sees: the
// event loop, and luna-service2's lifetime rules.

import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { after, before, test } from "node:test";
import { setTimeout as sleep } from "node:timers/promises";
import { setFlagsFromString } from "node:v8";
import { Worker } from "node:worker_threads";
import { runInNewContext } from "node:vm";

import type { BusMessage } from "#kit/handle.ts";
import { openHandle } from "#kit/lunabus.ts";
import { startTestBus, type TestBus } from "./hub.ts";

const EXITS = "com.webosce.test.exits";
const STAYS = "com.webosce.test.stays";
const FIRST = "com.webosce.test.first";
const SECOND = "com.webosce.test.second";
const ECHO = "com.webosce.test.echo";
const CLOSER = "com.webosce.test.closer";
const PINGER = "com.webosce.test.pinger";
const BUSY = "com.webosce.test.busy";

const env: { hub?: TestBus } = {};
const ignore = { onRequest: (_: BusMessage) => {}, onCancel: (_: BusMessage) => {} };

before(async () => {
    env.hub = await startTestBus({ services: [EXITS, STAYS, FIRST, SECOND, ECHO, CLOSER, PINGER, BUSY] });
});

after(() => env.hub?.stop());

// Runs a script in a child node with this process's environment and module
// resolution; resolves with its exit code, or "running" after `ms`.
const runScript = (source: string, ms: number): Promise<number | "running"> => new Promise((resolve) => {
    const child = spawn(process.execPath, ["--input-type=module", "-e", source], {
        cwd: new URL("..", import.meta.url).pathname,
        stdio: "inherit",
    });
    const timer = setTimeout(() => {
        child.kill("SIGKILL");
        resolve("running");
    }, ms);
    child.on("exit", (code) => {
        clearTimeout(timer);
        resolve(code ?? -1);
    });
});

const answering = (name: string) => openHandle(name, false, {
    onRequest: (message) => handles.get(name)?.respond(message, JSON.stringify({ from: name })),
    onCancel: () => {},
});
const handles = new Map<string, ReturnType<typeof openHandle>>();

const ask = (uri: string, timeoutMs = 2000): Promise<string> => new Promise((resolve, reject) => {
    const client = openHandle(null, false, ignore);
    const timer = setTimeout(() => {
        client.close();
        reject(new Error(`no reply from ${uri}`));
    }, timeoutMs);
    client.call(uri, "{}", true, (message) => {
        clearTimeout(timer);
        client.close();
        resolve(message.payload);
    });
});

test("a script ends by itself once it closes its handles", async () => {
    const code = await runScript(`
        import { openHandle } from "#kit/lunabus.ts";
        const handle = openHandle("${EXITS}", false, { onRequest() {}, onCancel() {} });
        handle.registerMethod("/", "ping");
        setTimeout(() => handle.close(), 200);
    `, 5000);
    assert.equal(code, 0);
});

test("a script with a handle open keeps running", async () => {
    const code = await runScript(`
        import { openHandle } from "#kit/lunabus.ts";
        globalThis.kept = openHandle("${STAYS}", false, { onRequest() {}, onCancel() {} });
    `, 1500);
    assert.equal(code, "running");
});

test("a handle opened after another was closed still hears its requests", async () => {
    // A service with a live subscriber, which it then loses, and closes: the
    // sequence that hung the suite when polls outlived their descriptors.
    const first = openHandle(FIRST, false, {
        onRequest: (message) => {
            first.subscriptionAdd(message.uniqueToken ?? "", message);
            first.respond(message, JSON.stringify({ from: FIRST }));
        },
        onCancel: () => {},
    });
    first.registerMethod("/", "watch");
    const subscriber = openHandle(null, false, ignore);
    await new Promise<void>((resolve) => {
        const token = subscriber.call(`luna://${FIRST}/watch`, JSON.stringify({ subscribe: true }), false, () => {
            subscriber.cancel(token);
            resolve();
        });
    });
    subscriber.close();
    await sleep(50);
    first.close();
    // The new sockets may reuse the closed ones' descriptor numbers.
    const second = answering(SECOND);
    handles.set(SECOND, second);
    second.registerMethod("/", "ping");
    try {
        assert.deepEqual(JSON.parse(await ask(`luna://${SECOND}/ping`)), { from: SECOND });
    } finally {
        second.close();
    }
});

test("closing a handle inside its own reply leaves the process working", async () => {
    const echo = answering(ECHO);
    handles.set(ECHO, echo);
    echo.registerMethod("/", "ping");
    try {
        const closedInside = await new Promise<boolean>((resolve) => {
            const client = openHandle(null, false, ignore);
            client.call(`luna://${ECHO}/ping`, "{}", true, () => {
                client.close();
                resolve(true);
            });
        });
        assert.equal(closedInside, true);
        await sleep(100);
        assert.deepEqual(JSON.parse(await ask(`luna://${ECHO}/ping`)), { from: ECHO });
    } finally {
        echo.close();
    }
});

test("a closed handle refuses to be used", () => {
    const handle = openHandle(null, false, ignore);
    handle.close();
    assert.throws(() => handle.call(`luna://${ECHO}/ping`, "{}", true, () => {}),
        { message: "the bus handle is closed" });
});

test("a one-reply call lets go of its callback once answered", async () => {
    setFlagsFromString("--expose-gc");
    const gc = runInNewContext("gc") as () => void;
    const pinger = answering(PINGER);
    handles.set(PINGER, pinger);
    pinger.registerMethod("/", "ping");
    const client = openHandle(null, false, ignore);
    const collected = { value: false };
    const registry = new FinalizationRegistry(() => { collected.value = true; });
    try {
        await new Promise<void>((resolve) => {
            const onResponse = () => resolve();
            registry.register(onResponse, "callback");
            client.call(`luna://${PINGER}/ping`, "{}", true, onResponse);
        });
        for (let i = 0; i < 50 && !collected.value; i++) {
            gc();
            await sleep(20);
        }
        assert.equal(collected.value, true, "the callback is still referenced");
    } finally {
        client.close();
        pinger.close();
    }
});

test("a handle closed while answering sends nothing", async () => {
    const results: boolean[] = [];
    const closer = openHandle(CLOSER, false, {
        onRequest: (message) => {
            closer.close();
            results.push(closer.respond(message, JSON.stringify({ late: true })));
        },
        onCancel: () => {},
    });
    closer.registerMethod("/", "ping");
    await assert.rejects(ask(`luna://${CLOSER}/ping`, 500), /no reply/);
    assert.deepEqual(results, [false]);
});

test("many clients coming and going are all answered", async () => {
    // Peers that disconnect and connect in the same dispatch hand the service
    // the same descriptor numbers; a poll kept on a reused number never hears
    // the new peer.
    const busy = answering(BUSY);
    handles.set(BUSY, busy);
    busy.registerMethod("/", "ping");
    const client = (rounds: number) => runScript(`
        import { openHandle } from "#kit/lunabus.ts";
        const handle = openHandle(null, false, { onRequest() {}, onCancel() {} });
        const ask = () => new Promise((resolve, reject) => {
            const timer = setTimeout(() => reject(new Error("no reply")), 3000);
            handle.call("luna://${BUSY}/ping", "{}", true, () => { clearTimeout(timer); resolve(); });
        });
        for (let i = 0; i < ${rounds}; i++) {
            await ask();
            await new Promise((resolve) => setTimeout(resolve, Math.random() * 20));
        }
        handle.close();
    `, 20000);
    try {
        for (let wave = 0; wave < 4; wave++) {
            const codes = await Promise.all(Array.from({ length: 12 }, (_, i) => client(1 + (i % 4))));
            assert.deepEqual(codes, codes.map(() => 0), `wave ${wave}: ${JSON.stringify(codes)}`);
        }
    } finally {
        busy.close();
    }
});

test("a worker thread may not load the addon", async () => {
    const worker = new Worker(`
        const { createRequire } = require("node:module");
        const { parentPort } = require("node:worker_threads");
        try {
            createRequire(process.env.WEBOS_LUNABUS)(process.env.WEBOS_LUNABUS);
            parentPort.postMessage("loaded");
        } catch (error) {
            parentPort.postMessage(String(error.message));
        }
    `, { eval: true });
    const outcome = await new Promise<string>((resolve) => worker.once("message", resolve));
    await worker.terminate();
    assert.equal(outcome, "lunabus.node runs on the main thread only");
});
