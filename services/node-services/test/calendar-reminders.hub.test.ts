// com.palm.service.calendar.reminders on a private hub with a real db8.
//
// The unit test (calendar-reminders.unit.test.ts) checks the logic against an
// in-memory db8. This checks the part only a real mojodb-luna can: that the two
// kinds register, that the reminder rows the service writes come back through
// db8's own indexes (showTime, eventId, autoCloseTime), and that the status
// singleton round-trips. The activity manager and the calendar app are played
// by this file -- the service's scheduling and app-launch calls land on fake
// bus handles that record them.

import assert from "node:assert/strict";
import { after, before, beforeEach, describe, test } from "node:test";
import { setTimeout as sleep } from "node:timers/promises";

import { createDb8, type Db8 } from "#kit/db8.ts";
import { createActivity, openBus, type Bus, type Payload } from "#kit/luna.ts";
import { createReminderService, type RunningService } from "../services/com.palm.service.calendar.reminders/service.ts";
import { EVENT_KIND, STATUS_KIND } from "../services/com.palm.service.calendar.reminders/reminders.ts";
import { REMINDER_KIND } from "../services/com.palm.service.calendar.reminders/scheduler.ts";
import { CONFIGURATOR, startTestBus, type TestBus } from "./hub.ts";

const SERVICE_NAME = "com.palm.service.calendar.reminders";
const SERVICE = `luna://${SERVICE_NAME}`;
const ACTIVITY_MANAGER = "com.palm.activitymanager";
const CALENDAR_APP = "com.palm.applicationManager";

interface Env {
    hub: TestBus;
    admin: Bus;
    db: Db8;
    service: RunningService;
    // The activity manager and calendar app, played here.
    am: Bus;
    app: Bus;
    amCalls: { method: string; payload: Payload }[];
    appCalls: { method: string; payload: Payload }[];
    activityId: number;
}
const env = {} as Env;

// A fixed "now" the service reads, so showTime math is deterministic.
const NOW = Date.UTC(2026, 5, 1, 12, 0, 0);

const until = async (condition: () => boolean | Promise<boolean>, what: string, ms = 4000) => {
    for (const started = Date.now(); Date.now() - started < ms; await sleep(20)) {
        if (await condition()) { return; }
    }
    assert.fail(`timed out waiting for ${what}`);
};

before(async () => {
    env.hub = await startTestBus({
        services: [SERVICE_NAME, ACTIVITY_MANAGER, CALENDAR_APP, "com.palm.calendar"],
        db8: true,
    });
    env.admin = openBus(CONFIGURATOR);
    await env.hub.startDb8(async () => {
        try {
            await env.admin.call("luna://com.palm.db/reserveIds", { count: 1 }, { timeout: 1 });
            return true;
        } catch {
            return false;
        }
    });
    env.db = createDb8(env.admin);

    // The calendar event kind the service reads (owned by the calendar app),
    // plus the service's own two kinds, registered as the configurator.
    await env.db.putKind({ id: EVENT_KIND, owner: "com.palm.calendar",
        indexes: [{ name: "rev", props: [{ name: "_rev" }] }] });

    // The grant HP's calendar app ships (configuration/db/permissions/
    // com.palm.calendarevent): the reminders service may read calendar events.
    // In the rootfs assemble-rootfs copies that file; here the configurator sets
    // it, so the real db8 lets the service's find through as it does on device.
    await env.admin.call("luna://com.palm.db/putPermissions", {
        permissions: [{
            type: "db.kind", object: EVENT_KIND, caller: SERVICE_NAME,
            operations: { read: "allow", delete: "allow" },
        }],
    });

    // The activity manager: create hands back an incrementing id; complete and
    // cancel just succeed. Recorded for assertions.
    env.amCalls = [];
    env.activityId = 0;
    env.am = openBus(ACTIVITY_MANAGER);
    for (const method of ["create", "complete", "cancel"]) {
        env.am.method(method, ({ payload }) => {
            env.amCalls.push({ method, payload });
            return method === "create" ? { returnValue: true, activityId: ++env.activityId } : { returnValue: true };
        });
    }

    // The calendar app: records every open.
    env.appCalls = [];
    env.app = openBus(CALENDAR_APP);
    env.app.method("open", ({ payload }) => {
        env.appCalls.push({ method: "open", payload });
        return { returnValue: true };
    });

    env.service = await createReminderService({
        openBus,
        createActivity: () => createActivity({
            setTimer: (callback, ms) => setTimeout(callback, ms),
            clearTimer: (timer) => clearTimeout(timer as ReturnType<typeof setTimeout> | undefined),
        }),
        now: () => NOW,
        idleMs: 60000,
        exit: () => {},
        log: () => {},
    })();
});

after(() => {
    env.service?.close();
    env.am?.close();
    env.app?.close();
    env.admin?.close();
    env.hub?.stop();
});

beforeEach(async () => {
    await env.db.delWhere({ from: REMINDER_KIND }, { purge: true });
    await env.db.delWhere({ from: STATUS_KIND }, { purge: true });
    await env.db.delWhere({ from: EVENT_KIND }, { purge: true });
    env.amCalls.length = 0;
    env.appCalls.length = 0;
});

const call = (method: string, payload: Payload = {}) => env.admin.call(`${SERVICE}/${method}`, payload);

describe("onInit against real db8", () => {
    test("it writes a reminder row that db8's showTime index finds, and the status singleton", async () => {
        const start = NOW + 3600000;
        await env.db.put([{ _kind: EVENT_KIND, _id: "ev1", subject: "Standup", dtstart: start, dtend: start + 1800000,
            allDay: false, calendarId: "cal1",
            alarm: [{ action: "DISPLAY", alarmTrigger: { valueType: "DURATION", value: "-PT15M" } }] } as never]);

        const reply = await call("onInit", {}) as Payload;
        assert.equal(reply.returnValue, true);

        // The reminder landed, and db8's own showTime index returns it.
        const byShow = await env.db.find({ from: REMINDER_KIND,
            where: [{ prop: "showTime", op: "=", val: start - 900000 }] });
        assert.equal(byShow.results.length, 1);
        assert.equal(byShow.results[0]!.eventId, "ev1");
        assert.equal(byShow.results[0]!.autoCloseTime, start + 1800000);

        // db8's eventId index too.
        const byEvent = await env.db.find({ from: REMINDER_KIND, where: [{ prop: "eventId", op: "=", val: "ev1" }] });
        assert.equal(byEvent.results.length, 1);

        // The status singleton, with the event's rev and OK.
        const status = await env.db.find({ from: STATUS_KIND });
        assert.equal(status.results.length, 1);
        assert.equal(status.results[0]!.status, "OK");

        // The three activities were created with the activity manager.
        await until(() => env.amCalls.filter((c) => c.method === "create").length >= 3, "three activities");
        const names = env.amCalls.filter((c) => c.method === "create")
            .map((c) => (c.payload.activity as Payload).name);
        assert.deepEqual([...names].sort(),
            ["calendar.reminders.autoclose", "calendar.reminders.dbchanged", "calendar.reminders.wake"]);
    });
});

describe("onWake against real db8", () => {
    test("it shows the reminder at a showTime and opens the calendar app", async () => {
        const show = NOW - 60000; // a reminder due a minute ago
        await env.db.put([{ _kind: REMINDER_KIND, eventId: "ev1", subject: "Standup",
            startTime: show, endTime: show + 1800000, alarmTime: show, showTime: show,
            autoCloseTime: show + 1800000, isRepeating: false } as never]);

        await call("onWake", { showTime: show });

        await until(() => env.appCalls.length >= 1, "the app was opened");
        const open = env.appCalls[0]!;
        assert.equal((open.payload as Payload).id, "com.palm.app.calendar");
        const alarm = ((open.payload as Payload).params as Payload).alarm as Payload[];
        assert.equal(alarm.length, 1);
        assert.equal(alarm[0]!.eventId, "ev1");
        // Times cross the bus as strings.
        assert.equal(typeof alarm[0]!.showTime, "string");
    });
});

describe("onSnooze against real db8", () => {
    test("it moves the reminder's showTime in db8", async () => {
        const [written] = await env.db.put([{ _kind: REMINDER_KIND, eventId: "ev1",
            startTime: NOW, endTime: NOW + 1, alarmTime: NOW, showTime: NOW, autoCloseTime: NOW + 900000,
            isRepeating: false } as never]);

        await call("onSnooze", { reminderId: written!.id, snoozeDuration: 600000 });

        const [row] = await env.db.get([written!.id]);
        assert.equal(row!.showTime, NOW + 600000);
    });
});
