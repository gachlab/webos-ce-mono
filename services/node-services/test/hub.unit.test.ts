// The test bus's own teardown: a daemon that will not leave must not keep the
// test run alive (#48).

import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { once } from "node:events";
import { test } from "node:test";

import { stopChildren } from "./hub.ts";

const spawnNode = (source: string) => spawn(process.execPath, ["-e", source], { stdio: ["ignore", "pipe", "ignore"] });

// Says so once its handler is in place, so the signal cannot arrive first.
const ready = async (child: ReturnType<typeof spawnNode>) => {
    await once(child.stdout!, "data");
};

test("a child that ignores SIGTERM is killed", async () => {
    const stubborn = spawnNode("process.on('SIGTERM', () => {}); console.log('up'); setInterval(() => {}, 1000);");
    await ready(stubborn);
    const exited = once(stubborn, "exit");
    stopChildren([stubborn], 200);
    // Bounded, and cleaned up either way: a broken stopChildren must fail
    // here, not leave the child to hang the run it is meant to protect.
    const timeout = new Promise<null>((resolve) => setTimeout(() => resolve(null), 3000).unref());
    const outcome = await Promise.race([exited, timeout]);
    if (outcome === null) {
        stubborn.kill("SIGKILL");
    }
    assert.deepEqual(outcome, [null, "SIGKILL"]);
});

test("a child that leaves on SIGTERM is not killed", async () => {
    const polite = spawnNode("console.log('up'); setInterval(() => {}, 1000);");
    await ready(polite);
    stopChildren([polite], 200);
    const [, signal] = await once(polite, "exit");
    assert.equal(signal, "SIGTERM");
});
