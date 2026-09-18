// A private luna-service2 bus, and a db8 on it, for tests.
//
// The hub's sockets have fixed paths under /tmp, so this must run where /tmp
// belongs to the test alone: test/run.sh puts the whole run in a namespace
// with its own /tmp (or relies on a throwaway container's).

import { spawn, type ChildProcess } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, openSync, realpathSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { setTimeout as sleep } from "node:timers/promises";

export interface TestBusOptions {
    // The service names tests will call.
    readonly services: readonly string[];
    // Whether the tests start db8 (startDb8), which needs its own names listed.
    readonly db8?: boolean;
}

export interface TestBus {
    readonly dir: string;
    // Starts db8 with an empty database; `ready` says when it answers.
    startDb8(ready: () => Promise<boolean>): Promise<void>;
    stop(): void;
}

export interface TestBusDeps {
    // Where build/staging is: the hub and db8 binaries.
    readonly staging: string;
    readonly spawn: typeof spawn;
    // When set, the daemons' output goes to files there.
    readonly logs: string | undefined;
}

const DB8 = "usr/sbin/mojodb-luna";

// The name a test uses to register db8 kinds, as com.palm.configurator does.
export const CONFIGURATOR = "com.webosce.test.configurator";

const waitFor = async (what: string, ready: () => boolean | Promise<boolean>, child: ChildProcess) => {
    for (let i = 0; i < 100; i++) {
        if (child.exitCode !== null) {
            throw new Error(`${what} exited with ${child.exitCode} before it was ready`);
        }
        if (await ready()) {
            return;
        }
        await sleep(50);
    }
    throw new Error(`${what} was not ready after 5 s`);
};

// How long a daemon gets to leave after SIGTERM before it is killed.
export const KILL_AFTER_MS = 2000;

// Asks each child to leave, and kills the ones that do not. A daemon that hangs
// on SIGTERM keeps this process alive, and --test-timeout does not end a test
// file whose tests are done: ls-hubd did exactly that, asleep in its own signal
// handler (#48), and a CI run sat for twenty minutes.
export const stopChildren = (children: readonly ChildProcess[], killAfterMs = KILL_AFTER_MS): void => {
    for (const child of children) {
        child.kill("SIGTERM");
        setTimeout(() => {
            if (child.exitCode === null && child.signalCode === null) {
                child.kill("SIGKILL");
            }
        }, killAfterMs).unref();
    }
};

// The hub only routes to names some .service file lists, even when the service
// is already up; a name nobody lists "does not exist". The Exec never runs for
// the tests' own services, which are up before anything calls them.
const hubConf = (dir: string, side: string, roles: string, names: readonly string[]): string => {
    const sockets = join(dir, `ls2-${side}`);
    const services = join(dir, `services-${side}`);
    mkdirSync(sockets);
    mkdirSync(services);
    if (names.length > 0) {
        writeFileSync(join(services, "tests.service"),
            `[D-BUS Service]\nName=${names.join(";")}\nExec=/bin/false\n`);
    }
    const conf = join(dir, `ls-${side}.conf`);
    writeFileSync(conf, [
        "[General]",
        `LocalSocketDirectory=${sockets}`,
        `PidDirectory=${sockets}`,
        "EnableStaticServices=false",
        "LogServiceStatus=true",
        "[Dynamic Services]",
        `Directories=${services}`,
        "LaunchTimeout=20000",
        "[Security]",
        "Enabled=false",
        `Directories=${roles}`,
        "",
    ].join("\n"));
    return conf;
};

// The hub refuses a service name unless a role file lets the calling
// executable use it, even with security off. It looks the executable up
// through /proc/<pid>/exe: this very node, and db8.
const writeRoles = (dir: string, executables: readonly string[]): string => {
    const roles = join(dir, "roles");
    mkdirSync(roles);
    executables.forEach((executable, i) => writeFileSync(join(roles, `role-${i}.json`), JSON.stringify({
        role: { exeName: realpathSync(executable), type: "privileged", allowedNames: ["*"] },
        permissions: [{ service: "*", inbound: ["*"], outbound: ["*"] }],
    })));
    return roles;
};

export const createTestBus = (deps: TestBusDeps) => async (options: TestBusOptions): Promise<TestBus> => {
    const dir = mkdtempSync(join(tmpdir(), "node-services-"));
    const children: ChildProcess[] = [];

    const output = (name: string): "ignore" | ["ignore", number, number] => {
        if (!deps.logs) {
            return "ignore";
        }
        mkdirSync(deps.logs, { recursive: true });
        const fd = openSync(join(deps.logs, `${name}.log`), "a");
        return ["ignore", fd, fd];
    };

    const start = (name: string, binary: string, args: string[]) => {
        const child = deps.spawn(join(deps.staging, binary), args, { stdio: output(name) });
        children.push(child);
        return child;
    };

    const stop = () => stopChildren([...children].reverse());

    try {
        const roles = writeRoles(dir, [process.execPath, join(deps.staging, DB8)]);
        const names = options.db8 ? [...options.services, "com.palm.db", "com.palm.tempdb", CONFIGURATOR] : options.services;
        for (const side of ["private", "public"] as const) {
            const conf = hubConf(dir, side, roles, names);
            const args = side === "public" ? ["--public", "--conf", conf] : ["--conf", conf];
            const hub = start(`ls-hubd-${side}`, "usr/sbin/ls-hubd", args);
            await waitFor(`ls-hubd (${side})`, () => existsSync(`/tmp/com.palm.${side}_hub`), hub);
        }
    } catch (error) {
        stop();
        throw error;
    }

    return {
        dir,
        startDb8: async (ready) => {
            const data = join(dir, "db8");
            mkdirSync(data);
            const conf = join(dir, "mojodb.conf");
            // The configurator may register any kind, as on the device.
            writeFileSync(conf, JSON.stringify({
                log: { appender: { type: "stderr" } },
                db: { permissions: [{ type: "db.role", object: "admin", caller: CONFIGURATOR, operations: { "*": "allow" } }] },
            }));
            await waitFor("mojodb-luna", ready, start("mojodb", DB8, ["-c", conf, data]));
        },
        stop,
    };
};

export const startTestBus = (options: TestBusOptions): Promise<TestBus> => {
    const staging = process.env.WEBOS_STAGING;
    if (!staging) {
        throw new Error("WEBOS_STAGING is not set; run the tests through test/run.sh");
    }
    return createTestBus({ staging, spawn, logs: process.env.WEBOS_TEST_LOGS })(options);
};
