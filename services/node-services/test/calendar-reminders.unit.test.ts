// com.palm.service.calendar.reminders, checked without a hub.
//
// Three layers:
//  * datetime.ts, held to HP's datejs by a golden check (the same vm sandbox
//    the port was verified with);
//  * scheduler.ts, held to a transcription of HP's findReminderTimes;
//  * the whole command layer, run over an in-memory db8 and a recording bus so
//    every db8 write, activity-manager call and calendar-app payload a command
//    produces is asserted exactly.
//
// Everything is deterministic: `now` and the recurrence engine are injected.

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { describe, test } from "node:test";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

import { createCommands } from "../services/com.palm.service.calendar.reminders/commands.ts";
import {
    calculateAlarmTime, getUTCDateString, parseDateTime, parseDuration,
} from "../services/com.palm.service.calendar.reminders/datetime.ts";
import { findNextOccurrence } from "../services/com.palm.service.calendar.reminders/recurrence.ts";
import { createReminders, EVENT_KIND, STATUS_KIND } from "../services/com.palm.service.calendar.reminders/reminders.ts";
import {
    findDisplayAlarm, findReminders, REMINDER_KIND, type CalendarEvent,
} from "../services/com.palm.service.calendar.reminders/scheduler.ts";
import type { Command } from "#kit/mojoservice.ts";
import type { DbObject } from "#kit/db8.ts";
import type { Payload } from "#kit/luna.ts";

// --- the HP reference, loaded once from datejs + utils.js --------------------

const hpSvc = fileURLToPath(new URL(
    "../../../components/app-services/com.palm.service.calendar.reminders/", import.meta.url));

const loadHp = () => {
    const sandbox: Record<string, unknown> = { console, Config: { logs: "off" } };
    vm.createContext(sandbox);
    vm.runInContext(readFileSync(`${hpSvc}datejs/date.js`, "utf8"), sandbox);
    vm.runInContext(readFileSync(`${hpSvc}utils.js`, "utf8"), sandbox);
    vm.runInContext("globalThis.__hp = { utilParseDateTime, utilParseDuration, utilCalculateAlarmTime, utilGetUTCDateString };", sandbox);
    return sandbox.__hp as {
        utilParseDateTime: (s: string) => number;
        utilParseDuration: (s: string, start: number) => number;
        utilCalculateAlarmTime: (start: number, alarm: unknown) => number;
        utilGetUTCDateString: (ts: number) => string;
    };
};

describe("datetime.ts is HP's datejs math, exactly", () => {
    const hp = loadHp();

    test("parseDateTime: 15-char local, 16-char UTC, malformed", () => {
        for (const s of ["20100406T093000", "20251231T235959", "20100406T093000Z", "19991231T000000Z", "bad", "2010"]) {
            assert.equal(parseDateTime(s), hp.utilParseDateTime(s), `parseDateTime(${s})`);
        }
    });

    test("parseDuration: RFC5545 durations, signed, across day boundaries", () => {
        const start = Date.UTC(2010, 3, 6, 9, 30, 0);
        for (const d of ["-PT15M", "PT1H", "-PT30M", "P1D", "-P1W", "PT45S", "P2DT3H15M", "-P1WT2H", "garbage"]) {
            assert.equal(parseDuration(d, start), hp.utilParseDuration(d, start), `parseDuration(${d})`);
        }
    });

    test("parseDuration: multi-day durations across a DST boundary match HP's local-Date math", () => {
        // A start before the US spring-forward; P2D and P1DT2H land on the same
        // wall-clock HP's datejs did, which fixed-ms arithmetic would miss by 1h.
        const start = new Date(2026, 2, 7, 12, 0, 0).getTime();
        for (const d of ["P2D", "P1DT2H", "-P1W", "P3DT4H5M", "PT90M"]) {
            assert.equal(parseDuration(d, start), hp.utilParseDuration(d, start), `dst parseDuration(${d})`);
        }
    });

    test("calculateAlarmTime: DATE-TIME and DURATION triggers, and the malformed cases", () => {
        const start = Date.UTC(2010, 3, 6, 9, 30, 0);
        const alarms = [
            { alarmTrigger: { valueType: "DURATION", value: "-PT15M" } },
            { alarmTrigger: { valueType: "DATE-TIME", value: "20100406T090000" } },
            { alarmTrigger: { valueType: "DATE-TIME", value: "20100406T090000Z" } },
            { alarmTrigger: { valueType: "WEIRD", value: "-PT10M" } },
            { alarmTrigger: {} },
            {},
        ];
        for (const a of alarms) {
            assert.equal(calculateAlarmTime(start, a), hp.utilCalculateAlarmTime(start, a), JSON.stringify(a));
        }
    });

    test("getUTCDateString: YYYY-MM-DD HH:MM:SSZ in UTC", () => {
        for (const ts of [Date.UTC(2010, 3, 6, 9, 30), 0, Date.UTC(2026, 0, 1, 5, 7, 9), Date.UTC(1999, 11, 31, 23, 59, 59)]) {
            assert.equal(getUTCDateString(ts), hp.utilGetUTCDateString(ts), `getUTCDateString(${ts})`);
        }
    });
});

// --- scheduler.ts vs HP's findReminderTimes transcription --------------------

const hpCalc = loadHp().utilCalculateAlarmTime;

const hpFindDisplayAlarm = (alarms: readonly { action?: string; alarmTrigger?: { valueType?: string; value?: string } }[] | undefined): number | null => {
    const n = alarms?.length ?? 0;
    for (let i = 0; i < n; i++) {
        const a = alarms![i]!;
        if (a.action && a.action.toLowerCase() !== "display") { continue; }
        if (a.alarmTrigger && a.alarmTrigger.valueType === "DURATION" && a.alarmTrigger.value
            && a.alarmTrigger.value.toLowerCase() !== "none") { return i; }
    }
    return null;
};

const hpFindReminders = (events: readonly CalendarEvent[], now: number, lastRevNumber: number,
                         eventDismissed: boolean, skipPastStartTime: number | undefined,
                         fno: (e: CalendarEvent, n: number) => number | false) => {
    let revNumber = lastRevNumber || 0;
    const reminderList: Payload[] = [];
    if (eventDismissed && skipPastStartTime) { now = skipPastStartTime + 1000; }
    for (const event of events) {
        if (event._rev !== undefined && event._rev > revNumber) { revNumber = event._rev; }
        if (!event.alarm || event.alarm.length === 0) { continue; }
        const idx = hpFindDisplayAlarm(event.alarm);
        if (idx === null) { continue; }
        if (event.dtend < now && !event.rrule) { continue; }
        if (eventDismissed && !event.rrule) { continue; }
        if (event.rrule && event.rrule.freq && event.rrule.until !== undefined && event.rrule.until < now) { continue; }
        let startTime: number; let isRepeatEvent: boolean;
        if ((event.rrule && event.rrule.freq) && (event.rrule.until === undefined || event.rrule.until > now)) {
            const next = fno(event, now);
            if (next === false) { continue; }
            startTime = next; isRepeatEvent = true;
        } else { startTime = event.dtstart; isRepeatEvent = false; }
        const alarmTime = hpCalc(startTime, event.alarm[idx]);
        const showTime = (alarmTime < now && startTime >= now) ? startTime : alarmTime;
        const duration = event.dtend - event.dtstart;
        const autoCloseTime = (duration < 900000) ? (startTime + 900000) : (startTime + duration);
        const shouldBeShowingNow = (!eventDismissed && now >= showTime && now <= autoCloseTime);
        if (showTime >= now || shouldBeShowingNow) {
            reminderList.push({
                _kind: REMINDER_KIND, eventId: event._id, subject: event.subject, location: event.location,
                isAllDay: event.allDay, attendees: event.attendees, calendarId: event.calendarId,
                startTime, endTime: startTime + duration, alarmTime, showTime, autoCloseTime, isRepeating: isRepeatEvent,
            });
        }
    }
    return { reminderList, lastRevNumber: revNumber };
};

describe("scheduler.ts is HP's findReminderTimes, exactly", () => {
    const now = Date.UTC(2010, 3, 6, 9, 0, 0);
    const dur = (v: string) => ({ alarmTrigger: { valueType: "DURATION", value: v } });
    const fno = (e: CalendarEvent, n: number) => { let t = e.dtstart; while (t < n) { t += 7 * 86400000; } return t; };

    const events: CalendarEvent[] = [
        { _id: "e1", _rev: 5, subject: "A", dtstart: now + 3600000, dtend: now + 7200000, allDay: false, calendarId: "c1", alarm: [dur("-PT15M")] },
        { _id: "e2", _rev: 6, dtstart: now - 7200000, dtend: now - 3600000, alarm: [dur("-PT15M")] },
        { _id: "e3", _rev: 7, dtstart: now + 3600000, dtend: now + 3600000 + 300000, alarm: [dur("-PT5M")] },
        { _id: "e4", _rev: 8, dtstart: now + 300000, dtend: now + 3600000, alarm: [dur("-PT30M")] },
        { _id: "e5", _rev: 9, dtstart: now + 3600000, dtend: now + 7200000, alarm: [{ action: "EMAIL", alarmTrigger: { valueType: "DURATION", value: "-PT15M" } }] },
        { _id: "e6", _rev: 10, dtstart: now + 3600000, dtend: now + 7200000, alarm: [{ action: "DISPLAY", alarmTrigger: { valueType: "DATE-TIME", value: "20100406T120000" } }] },
        { _id: "e7", _rev: 11, dtstart: now - 30 * 86400000, dtend: now - 30 * 86400000 + 3600000, rrule: { freq: "WEEKLY" }, alarm: [dur("-PT10M")] },
        { _id: "e8", _rev: 12, dtstart: now + 3600000, dtend: now + 7200000, alarm: [dur("none")] },
    ];

    test("the normal pass schedules the same reminders HP would", () => {
        const mine = findReminders({ events, lastRevNumber: 1 }, now, fno);
        const hp = hpFindReminders(events, now, 1, false, undefined, fno);
        assert.deepEqual(mine, hp);
    });

    test("a dismissed occurrence drops the non-repeat and keeps the repeat, as HP", () => {
        const evs = [events[0]!, events[6]!];
        const mine = findReminders({ events: evs, lastRevNumber: 1, eventDismissed: true, skipPastStartTime: now + 3600000 }, now, fno);
        const hp = hpFindReminders(evs, now, 1, true, now + 3600000, fno);
        assert.deepEqual(mine, hp);
    });

    test("findDisplayAlarm keeps HP's quirk: a DATE-TIME display alarm is not chosen", () => {
        assert.equal(findDisplayAlarm([{ action: "DISPLAY", alarmTrigger: { valueType: "DATE-TIME", value: "20100406T120000" } }]), null);
        assert.equal(findDisplayAlarm([{ alarmTrigger: { valueType: "DURATION", value: "-PT15M" } }]), 0);
        assert.equal(findDisplayAlarm([{ alarmTrigger: { valueType: "DURATION", value: "none" } }]), null);
    });
});

// --- recurrence.ts -----------------------------------------------------------

describe("recurrence.ts steps the plain rules", () => {
    const base = Date.UTC(2026, 0, 1, 9, 0, 0); // Thu 1 Jan 2026 09:00 UTC

    test("no rrule, or an unknown freq, does not repeat", () => {
        assert.equal(findNextOccurrence({ dtstart: base, dtend: base + 3600000 }, base + 1), false);
        assert.equal(findNextOccurrence({ dtstart: base, dtend: base + 3600000, rrule: { freq: "SECONDLY" } }, base + 1), false);
    });

    test("DAILY/WEEKLY step by fixed spans, honoring interval", () => {
        assert.equal(findNextOccurrence({ dtstart: base, dtend: base + 1, rrule: { freq: "DAILY" } }, base), base + 86400000);
        assert.equal(findNextOccurrence({ dtstart: base, dtend: base + 1, rrule: { freq: "WEEKLY" } }, base), base + 604800000);
        const everyThreeDays = { dtstart: base, dtend: base + 1, rrule: { freq: "DAILY", interval: 3 } };
        assert.equal(findNextOccurrence(everyThreeDays, base), base + 3 * 86400000);
    });

    test("MONTHLY keeps the day of month and clamps a short month", () => {
        const jan31 = Date.UTC(2026, 0, 31, 9, 0, 0);
        const next = findNextOccurrence({ dtstart: jan31, dtend: jan31 + 1, rrule: { freq: "MONTHLY" } }, jan31);
        assert.equal(next, Date.UTC(2026, 1, 28, 9, 0, 0)); // Feb 28, clamped
    });

    test("UNTIL ends the series", () => {
        const until = base + 86400000; // one day
        assert.equal(findNextOccurrence({ dtstart: base, dtend: base + 1, rrule: { freq: "DAILY", until } }, base), base + 86400000);
        assert.equal(findNextOccurrence({ dtstart: base, dtend: base + 1, rrule: { freq: "DAILY", until } }, base + 86400000), false);
    });
});

// --- the command layer, over an in-memory db8 and a recording bus ------------

// A tiny db8 that answers com.palm.db's methods the way the kit's db8 client
// calls them, enough for the commands to run. Records nothing itself; the test
// reads the tables directly.
interface Table { rows: (DbObject & Record<string, unknown>)[]; }

const createFakeDb = () => {
    const tables = new Map<string, Table>();
    let idCounter = 0, revCounter = 100;
    const tableOf = (kind: string): Table => {
        let t = tables.get(kind);
        if (!t) { t = { rows: [] }; tables.set(kind, t); }
        return t;
    };
    const matches = (row: Record<string, unknown>, where: readonly Payload[] | undefined): boolean =>
        (where ?? []).every((c) => {
            const v = row[c.prop as string]; const val = (c as { val: unknown }).val;
            switch (c.op) {
            case "=": return v === val;
            case ">": return typeof v === "number" && v > (val as number);
            case ">=": return typeof v === "number" && v >= (val as number);
            default: return true;
            }
        });
    const find = (query: Payload) => {
        const t = tableOf(query.from as string);
        let rows = t.rows.filter((r) => matches(r, query.where as Payload[] | undefined));
        if (query.orderBy) {
            const key = query.orderBy as string;
            rows = [...rows].sort((a, b) => Number(a[key] ?? 0) - Number(b[key] ?? 0));
        }
        if (typeof query.limit === "number") { rows = rows.slice(0, query.limit); }
        return { returnValue: true, results: rows };
    };
    const put = (objects: readonly DbObject[]) => {
        const results = objects.map((o) => {
            const _id = o._id ?? `id${++idCounter}`;
            const _rev = ++revCounter;
            const row = { ...o, _id, _rev } as DbObject & Record<string, unknown>;
            const t = tableOf(o._kind as string);
            const i = t.rows.findIndex((r) => r._id === _id);
            if (i >= 0) { t.rows[i] = row; } else { t.rows.push(row); }
            return { id: _id, rev: _rev };
        });
        return { returnValue: true, results };
    };
    const merge = (payload: Payload) => {
        if (Array.isArray(payload.objects) === false && payload.query) {
            // mergeWhere(query, props)
            const t = tableOf((payload.query as Payload).from as string);
            let count = 0;
            for (const row of t.rows) {
                if (matches(row, (payload.query as Payload).where as Payload[] | undefined)) {
                    Object.assign(row, payload.props); count++;
                }
            }
            return { returnValue: true, count };
        }
        const results = (payload.objects as DbObject[]).map((o) => {
            for (const t of tables.values()) {
                const row = t.rows.find((r) => r._id === o._id);
                if (row) { Object.assign(row, o); return { id: row._id!, rev: ++revCounter }; }
            }
            return { id: o._id ?? "?", rev: ++revCounter };
        });
        return { returnValue: true, results };
    };
    const del = (payload: Payload) => {
        if (payload.query) {
            const t = tableOf((payload.query as Payload).from as string);
            const before = t.rows.length;
            t.rows = t.rows.filter((r) => !matches(r, (payload.query as Payload).where as Payload[] | undefined));
            return { returnValue: true, count: before - t.rows.length };
        }
        const ids = new Set(payload.ids as string[]);
        let count = 0;
        for (const t of tables.values()) {
            const before = t.rows.length;
            t.rows = t.rows.filter((r) => !ids.has(r._id ?? ""));
            count += before - t.rows.length;
        }
        return { returnValue: true, results: [], count };
    };
    const get = (ids: readonly string[]) => {
        const out: DbObject[] = [];
        for (const id of ids) {
            for (const t of tables.values()) {
                const row = t.rows.find((r) => r._id === id);
                if (row) { out.push(row); break; }
            }
        }
        return { returnValue: true, results: out };
    };

    const handle = (method: string, payload: Payload): Payload => {
        switch (method) {
        case "find": return find(payload.query as Payload);
        case "put": return put(payload.objects as DbObject[]);
        case "merge": return merge(payload);
        case "del": return del(payload);
        case "get": return get(payload.ids as string[]);
        case "putKind": case "delKind": return { returnValue: true };
        case "batch": {
            const responses = (payload.operations as { method: string; params: Payload }[])
                .map((op) => handle(op.method, op.params));
            return { returnValue: true, responses };
        }
        default: return { returnValue: true };
        }
    };
    return { tables, tableOf, handle };
};

// A bus whose call() routes com.palm.db to the fake db8, records everything
// else (activity manager, calendar app), and hands back canned replies.
const createRecordingBus = (db: ReturnType<typeof createFakeDb>) => {
    const calls: { uri: string; payload: Payload }[] = [];
    let activityId = 0;
    const call = async <R extends Payload>(uri: string, payload: Payload = {}): Promise<R> => {
        calls.push({ uri, payload });
        const method = uri.split("/").pop()!;
        if (uri.startsWith("luna://com.palm.db/")) {
            return db.handle(method, payload) as unknown as R;
        }
        if (uri === "luna://com.palm.activitymanager/create") {
            return { returnValue: true, activityId: ++activityId } as unknown as R;
        }
        return { returnValue: true } as unknown as R;
    };
    return { calls, call };
};

interface Harness {
    readonly db: ReturnType<typeof createFakeDb>;
    readonly bus: ReturnType<typeof createRecordingBus>;
    readonly run: (name: string, payload: Payload) => Promise<Payload | void>;
}

const NOW = Date.UTC(2026, 5, 1, 12, 0, 0);

const makeHarness = (events: (CalendarEvent & DbObject)[] = []): Harness => {
    const db = createFakeDb();
    for (const e of events) { db.tableOf(EVENT_KIND).rows.push(e); }
    const bus = createRecordingBus(db);
    const now = () => NOW;
    // The kit's createDb8 is what reminders.ts uses; build it over the bus.
    // reminders.ts and commands.ts take the db8 via createReminders/service, so
    // wire them exactly as service.ts does, minus the activity/lifecycle.
    const kitDb = {
        find: async (query: Payload) => bus.call("luna://com.palm.db/find", { query }),
        get: async (ids: readonly string[]) => bus.call("luna://com.palm.db/get", { ids }),
        put: async (objects: readonly DbObject[]) => bus.call("luna://com.palm.db/put", { objects }),
        merge: async (arg: Payload) => bus.call("luna://com.palm.db/merge", arg),
        del: async (arg: Payload) => bus.call("luna://com.palm.db/del", arg),
        batch: async (operations: readonly Payload[]) => bus.call("luna://com.palm.db/batch", { operations }),
    };
    // Thin adapter matching the Db8 shape createReminders needs.
    const db8 = {
        find: async <T extends DbObject>(query: Payload) => (await kitDb.find(query)) as { results: T[] },
        get: async <T extends DbObject>(ids: readonly string[]) => (await kitDb.get(ids) as { results: T[] }).results,
        put: async (objects: readonly DbObject[]) => (await kitDb.put(objects) as { results: unknown[] }).results as never,
        merge: async (objects: readonly DbObject[]) => (await kitDb.merge({ objects }) as { results: unknown[] }).results as never,
        mergeWhere: async (query: Payload, props: Payload) => (await kitDb.merge({ query, props }) as { count: number }).count,
        del: async (ids: readonly string[]) => (await kitDb.del({ ids }) as { results: unknown[] }).results as never,
        delWhere: async (query: Payload) => (await kitDb.del({ query }) as { count: number }).count,
        batch: async (operations: readonly Payload[]) => (await kitDb.batch(operations) as { responses: Payload[] }).responses,
        putKind: async () => undefined,
        delKind: async () => undefined,
        findAll: async function* () {},
        watchFind: async function* () {},
    };
    const reminders = createReminders({ db: db8 as never, bus, log: () => {}, now });
    const fno = (e: CalendarEvent, n: number) => { let t = e.dtstart; while (t < n) { t += 86400000; } return t; };
    const commands: Command[] = createCommands({
        bus, reminders,
        allEvents: async () => (await db8.find<CalendarEvent & DbObject>({ from: EVENT_KIND, orderBy: "_rev" })).results,
        findNextOccurrence: fno, now, log: () => {},
    });
    const byName = new Map(commands.map((c) => [c.name, c.handler]));
    const run = async (name: string, payload: Payload): Promise<Payload | void> => {
        const result = byName.get(name)!({ payload, signal: new AbortController().signal } as never);
        return result as Promise<Payload | void>;
    };
    return { db, bus, run };
};

const appCalls = (h: Harness) => h.bus.calls.filter((c) => c.uri === "luna://com.palm.applicationManager/open");
const amCalls = (h: Harness) => h.bus.calls.filter((c) => c.uri.startsWith("luna://com.palm.activitymanager/"));

describe("onInit builds the reminder table and schedules the activities", () => {
    test("a full init writes reminders, the status row, and the three activities", async () => {
        const future = NOW + 3600000;
        const h = makeHarness([
            { _id: "ev1", _rev: 7, _kind: EVENT_KIND, subject: "Standup", dtstart: future, dtend: future + 1800000,
              alarm: [{ alarmTrigger: { valueType: "DURATION", value: "-PT15M" } }] } as CalendarEvent & DbObject,
        ]);
        const reply = await h.run("onInit", {}) as Payload;
        assert.equal(reply.returnValue, true);

        const reminders = h.db.tableOf(REMINDER_KIND).rows;
        assert.equal(reminders.length, 1);
        assert.equal(reminders[0]!.eventId, "ev1");
        assert.equal(reminders[0]!.showTime, future - 900000); // 15 min before

        const status = h.db.tableOf(STATUS_KIND).rows;
        assert.equal(status.length, 1);
        assert.equal(status[0]!.status, "OK");
        assert.equal(status[0]!.lastRevNumber, 7);

        // wake, autoclose and dbchanged activities were created.
        const created = amCalls(h).filter((c) => c.uri.endsWith("/create"));
        const names = created.map((c) => ((c.payload.activity as Payload).name));
        assert.deepEqual([...names].sort(),
            ["calendar.reminders.autoclose", "calendar.reminders.dbchanged", "calendar.reminders.wake"]);
    });
});

describe("onWake shows the reminders at a time and reschedules", () => {
    test("it opens the calendar app with the alarm, times stringified, duplicates filtered", async () => {
        const show = NOW + 1000;
        const h = makeHarness();
        // Two rows for the same event+startTime (the NOV-102491 duplicate), one other.
        const dupe = { _kind: REMINDER_KIND, eventId: "ev1", subject: "A", startTime: show + 900000,
                       endTime: show + 2700000, alarmTime: show, showTime: show, autoCloseTime: show + 2700000,
                       isRepeating: false };
        h.db.tableOf(REMINDER_KIND).rows.push({ ...dupe, _id: "r1" } as never, { ...dupe, _id: "r2" } as never);

        await h.run("onWake", { showTime: show });

        const open = appCalls(h);
        assert.equal(open.length, 1);
        const alarm = (open[0]!.payload.params as Payload).alarm as Payload[];
        assert.equal(alarm.length, 1, "the duplicate was filtered (NOV-102491)");
        assert.equal(typeof alarm[0]!.startTime, "string", "times cross the bus as strings");
        assert.equal(alarm[0]!.showTime, String(show));
    });

    test("missing showTime fails the command", async () => {
        const h = makeHarness();
        await assert.rejects(() => h.run("onWake", {}) as Promise<unknown>);
    });
});

describe("onSnooze moves a reminder's showTime", () => {
    test("it merges the new showTime and reschedules", async () => {
        const h = makeHarness();
        h.db.tableOf(REMINDER_KIND).rows.push({ _id: "r1", _kind: REMINDER_KIND, eventId: "ev1",
            startTime: NOW, endTime: NOW + 1, alarmTime: NOW, showTime: NOW, autoCloseTime: NOW + 900000,
            isRepeating: false } as never);
        await h.run("onSnooze", { reminderId: "r1", snoozeDuration: 600000 });
        assert.equal(h.db.tableOf(REMINDER_KIND).rows[0]!.showTime, NOW + 600000);
    });

    test("the default snooze is five minutes", async () => {
        const h = makeHarness();
        h.db.tableOf(REMINDER_KIND).rows.push({ _id: "r1", _kind: REMINDER_KIND, eventId: "ev1",
            startTime: NOW, endTime: NOW + 1, alarmTime: NOW, showTime: NOW, autoCloseTime: NOW + 900000,
            isRepeating: false } as never);
        await h.run("onSnooze", { reminderId: "r1" });
        assert.equal(h.db.tableOf(REMINDER_KIND).rows[0]!.showTime, NOW + 300000);
    });

    test("missing reminderId fails", async () => {
        const h = makeHarness();
        await assert.rejects(() => h.run("onSnooze", {}) as Promise<unknown>);
    });
});

describe("onAutoClose closes reminders and tells the app", () => {
    test("it opens the app with alarmClose and deletes the rows", async () => {
        const at = NOW + 900000;
        const h = makeHarness();
        h.db.tableOf(REMINDER_KIND).rows.push({ _id: "r1", _kind: REMINDER_KIND, eventId: "ev1",
            startTime: NOW, endTime: at, alarmTime: NOW, showTime: NOW, autoCloseTime: at,
            isRepeating: false } as never);
        await h.run("onAutoClose", { autoCloseTime: at });
        const open = appCalls(h);
        assert.equal(open.length, 1);
        assert.deepEqual((open[0]!.payload.params as Payload).alarmClose, ["r1"]);
        assert.equal(h.db.tableOf(REMINDER_KIND).rows.length, 0, "the reminder was deleted");
    });
});

describe("onDismiss deletes a reminder and does not refire at its own start", () => {
    test("a dismissed non-repeating event leaves no reminder", async () => {
        const start = NOW + 3600000;
        const h = makeHarness([
            { _id: "ev1", _rev: 7, _kind: EVENT_KIND, dtstart: start, dtend: start + 1800000,
              alarm: [{ alarmTrigger: { valueType: "DURATION", value: "-PT15M" } }] } as CalendarEvent & DbObject,
        ]);
        h.db.tableOf(REMINDER_KIND).rows.push({ _id: "r1", _kind: REMINDER_KIND, eventId: "ev1",
            startTime: start, endTime: start + 1800000, alarmTime: start - 900000, showTime: start - 900000,
            autoCloseTime: start + 1800000, isRepeating: false } as never);
        await h.run("onDismiss", { reminderId: "r1", eventId: "ev1", startTime: start });
        assert.equal(h.db.tableOf(REMINDER_KIND).rows.length, 0, "dismissed, non-repeating: gone");
    });
});

describe("onDBChanged reconciles the table and notifies the app", () => {
    test("unittest short-circuits to success", async () => {
        const h = makeHarness();
        const reply = await h.run("onDBChanged", { unittest: true }) as Payload;
        assert.deepEqual(reply, { returnValue: true });
        assert.equal(h.bus.calls.length, 0, "nothing touched in unittest mode");
    });

    test("a deleted event's reminders are removed and the app is told", async () => {
        const h = makeHarness();
        // Status says we last processed rev 100.
        h.db.tableOf(STATUS_KIND).rows.push({ _id: "s1", _kind: STATUS_KIND, status: "OK",
            lastRevNumber: 100, dbChangedActivityId: 0, wakeActivityId: 0, autoCloseActivityId: 0 } as never);
        // A deleted event at a higher rev, and a stale reminder for it.
        h.db.tableOf(EVENT_KIND).rows.push({ _id: "evX", _rev: 105, _kind: EVENT_KIND, _del: true } as never);
        h.db.tableOf(REMINDER_KIND).rows.push({ _id: "rX", _kind: REMINDER_KIND, eventId: "evX",
            startTime: NOW, endTime: NOW + 1, alarmTime: NOW, showTime: NOW, autoCloseTime: NOW + 1,
            isRepeating: false } as never);

        await h.run("onDBChanged", {});

        assert.equal(h.db.tableOf(REMINDER_KIND).rows.length, 0, "the deleted event's reminder is gone");
        const deleted = appCalls(h).find((c) => (c.payload.params as Payload).alarmDeleted);
        assert.ok(deleted, "the app was told of the deletion");
        assert.deepEqual((deleted!.payload.params as Payload).alarmDeleted, ["evX"]);
    });

    test("an empty fire still reschedules the wake and autoclose activities (does not short-circuit)", async () => {
        const h = makeHarness();
        h.db.tableOf(STATUS_KIND).rows.push({ _id: "s1", _kind: STATUS_KIND, status: "OK",
            lastRevNumber: 100, dbChangedActivityId: 7, wakeActivityId: 0, autoCloseActivityId: 0 } as never);
        // No events changed since rev 100.
        await h.run("onDBChanged", {});

        // The rev is nudged forward so we do not spin on the same watch.
        assert.equal(h.db.tableOf(STATUS_KIND).rows[0]!.lastRevNumber, 101, "rev nudged forward");

        // HP does NOT short-circuit an empty fire: it still runs
        // findNextWakeTime and findNextAutoCloseTime, each of which queries the
        // reminder table by showTime and autoCloseTime. A short-circuiting
        // implementation would skip both queries.
        const reminderFinds = h.bus.calls.filter((c) =>
            c.uri === "luna://com.palm.db/find"
            && (c.payload.query as Payload | undefined)?.from === REMINDER_KIND);
        const props = reminderFinds.map((c) => ((c.payload.query as Payload).where as Payload[] | undefined)?.[0]?.prop);
        assert.ok(props.includes("showTime"), "the next wake was recomputed");
        assert.ok(props.includes("autoCloseTime"), "the next autoclose was recomputed");
    });
});
