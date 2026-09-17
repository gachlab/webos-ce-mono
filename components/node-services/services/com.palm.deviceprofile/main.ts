// Entry point: `node main.ts`, started by the hub on demand.

import { randomBytes } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { machine } from "node:os";
import { dirname } from "node:path";

import { createActivity, openBus } from "#kit/luna.ts";
import { newNduId } from "./profile.ts";
import { createDeviceProfileService } from "./service.ts";

const NDUID = process.env.WEBOS_NDUID_FILE ?? "/var/palm/data/com.palm.deviceprofile/nduid";
const BUILD_INFO = process.env.WEBOS_BUILD_INFO ?? "/etc/palm/palm-build-info";
const DMI = "/sys/class/dmi/id";

const read = (path: string): string => {
    try {
        return readFileSync(path, "utf8").trim();
    } catch {
        return "";
    }
};

// Made once per install and kept.
const nduId = (): string => {
    const stored = read(NDUID);
    if (/^[0-9a-f]{40}$/.test(stored)) {
        return stored;
    }
    const id = newNduId((n) => randomBytes(n));
    if (!existsSync(dirname(NDUID))) {
        mkdirSync(dirname(NDUID), { recursive: true });
    }
    writeFileSync(NDUID, `${id}\n`);
    return id;
};

createDeviceProfileService({
    openBus,
    createActivity: () => createActivity({
        setTimer: (callback, ms) => setTimeout(callback, ms),
        clearTimer: (timer) => clearTimeout(timer as ReturnType<typeof setTimeout> | undefined),
    }),
    exit: () => process.exit(0),
    facts: () => ({
        nduId: nduId(),
        model: read(`${DMI}/product_name`),
        hardwareVersion: read(`${DMI}/product_version`),
        machine: machine(),
        buildInfo: read(BUILD_INFO),
    }),
})();
