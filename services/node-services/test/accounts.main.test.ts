// services/com.palm.service.accounts/main.ts as the hub would start it: its own
// process, the real files and wiring, answering on the bus and quitting.

import assert from "node:assert/strict";
import { spawn, type ChildProcess } from "node:child_process";
import { cpSync } from "node:fs";
import { join } from "node:path";
import { after, before, test } from "node:test";
import { setTimeout as sleep } from "node:timers/promises";

import { openBus, type Bus, type Payload } from "#kit/luna.ts";
import { SERVICE_NAME } from "../services/com.palm.service.accounts/service.ts";
import { startTestBus, type TestBus } from "./hub.ts";

const PROFILE = new URL("../../../components/app-services/account-templates/palmprofile/com.palm.palmprofile/", import.meta.url).pathname;
const MAIN = new URL("../services/com.palm.service.accounts/main.ts", import.meta.url).pathname;

const env: { hub?: TestBus; client?: Bus; service?: ChildProcess; exited?: Promise<number | null> } = {};

before(async () => {
    env.hub = await startTestBus({ services: [SERVICE_NAME] });
    const templates = join(env.hub.dir, "templates");
    cpSync(PROFILE, join(templates, "com.palm.palmprofile"), { recursive: true });
    env.service = spawn(process.execPath, [MAIN], {
        env: { ...process.env, WEBOS_ACCOUNT_TEMPLATES: templates },
        stdio: ["ignore", "ignore", "inherit"],
    });
    env.exited = new Promise((resolve) => env.service!.on("exit", (code) => resolve(code)));
    env.client = openBus(null);
});

after(() => {
    env.client?.close();
    env.service?.kill("SIGKILL");
    env.hub?.stop();
});

test("the service starts, lists the installed templates, and quits when told", async () => {
    let reply: Payload | undefined;
    for (let i = 0; i < 100 && !reply; i++) {
        reply = await env.client!.call(`luna://${SERVICE_NAME}/listAccountTemplates`, {}, { timeout: 1 }).catch(() => undefined);
        if (!reply) {
            await sleep(50);
        }
    }
    assert.ok(reply, "the service never answered");
    assert.deepEqual((reply.results as Payload[]).map((t) => t.templateId), ["com.palm.palmprofile"]);
    assert.match(String((reply.results as Payload[])[0]!.icon && ((reply.results as Payload[])[0]!.icon as Payload).loc_32x32),
        /com\.palm\.palmprofile\/images\/mypalm-32x32\.png$/);
    await env.client!.call(`luna://${SERVICE_NAME}/__quit`);
    assert.equal(await env.exited, 0);
});
