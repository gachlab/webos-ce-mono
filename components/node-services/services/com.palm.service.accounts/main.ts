// Entry point: `node main.ts`, started by the hub on demand.

import { closeSync, openSync, readdirSync, readFileSync, statSync } from "node:fs";
import { setTimeout as sleep } from "node:timers/promises";

import { createActivity, openBus } from "#kit/luna.ts";
import type { Schema } from "#kit/json-schema.ts";
import { createAccountsService } from "./service.ts";
import { TEMPLATE_ROOTS } from "./templates.ts";

const schema = (name: string): Schema =>
    JSON.parse(readFileSync(new URL(`schemas/${name}`, import.meta.url), "utf8")) as Schema;

const stat = (path: string) => {
    try {
        return statSync(path);
    } catch {
        return undefined;
    }
};

// First use checks this flag for a profile account made without it.
const PROFILE_FLAG = "/var/luna/preferences/first-use-profile-created";

await createAccountsService({
    openBus,
    createActivity: () => createActivity({
        setTimer: (callback, ms) => setTimeout(callback, ms),
        clearTimer: (timer) => clearTimeout(timer as ReturnType<typeof setTimeout> | undefined),
    }),
    files: {
        list: (dir) => {
            try {
                return readdirSync(dir);
            } catch {
                return [];
            }
        },
        isDirectory: (path) => stat(path)?.isDirectory() ?? false,
        isFile: (path) => stat(path)?.isFile() ?? false,
        read: (path) => {
            try {
                return readFileSync(path, "utf8");
            } catch {
                return undefined;
            }
        },
    },
    templateRoots: (process.env.WEBOS_ACCOUNT_TEMPLATES?.split(":") ?? TEMPLATE_ROOTS),
    schemas: { file: schema("template-file.json"), template: schema("template.json") },
    sleep: (ms) => sleep(ms),
    markProfileCreated: () => {
        if (!stat(PROFILE_FLAG)) {
            closeSync(openSync(PROFILE_FLAG, "w"));
        }
    },
    exit: () => process.exit(0),
    log: (message) => console.log(`com.palm.service.accounts: ${message}`),
})();
