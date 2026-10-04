// The stateful half of com.palm.service.calendar.reminders: what it writes to
// db8 and what it schedules with the activity manager. Rewritten from
// reminder-assistant.js's subroutines (findNextWakeTimeSubroutine,
// createOnWakeActivity, saveWakeActivityId and their autoclose and db-watch
// twins, plus the status-table recovery), with HP's Future chains as
// async/await.
//
// scheduler.ts decided WHICH reminders should exist and WHEN; this writes them,
// and schedules the next wake, the next auto-close and the db watch -- all as
// com.palm.activitymanager activities, exactly as HP did, so a sleeping device
// still wakes to show a reminder. The singleton status row
// (com.palm.service.calendar.remindersstatus:1) remembers the activity ids and
// the last db revision across restarts; a failure anywhere degrades it so the
// next onInit/onDBChanged starts over cleanly.
//
// Everything here takes its db8 and bus through `deps`, so the command layer
// and the tests wire the same calls to a real hub or a fake one.

import type { Db8, DbObject } from "#kit/db8.ts";
import type { Bus, Payload } from "#kit/luna.ts";
import { getUTCDateString } from "./datetime.ts";
import { type CalendarEvent, type Reminder, REMINDER_KIND } from "./scheduler.ts";

export const STATUS_KIND = "com.palm.service.calendar.remindersstatus:1";
export const EVENT_KIND = "com.palm.calendarevent:1";
const SELF = "luna://com.palm.service.calendar.reminders";
const ACTIVITY_MANAGER = "luna://com.palm.activitymanager";

// The singleton status row.
export interface Status extends DbObject {
    lastRevNumber?: number;
    status?: string;
    wakeActivityId?: number;
    autoCloseActivityId?: number;
    dbChangedActivityId?: number;
}

export interface RemindersDeps {
    readonly db: Db8;
    readonly bus: Pick<Bus, "call">;
    readonly log: (message: string) => void;
    // new Date() with the milliseconds zeroed, as HP's subroutines did.
    readonly now: () => number;
}

// What createOnWakeActivity decided to do, kept so saveWakeActivityId can
// record it the way HP's two-step did.
type StatusAction = "none" | "completed" | "created" | "updated" | "failed";

export interface Reminders {
    // What prepareForTakeoff returned: the last rev and the db-watch activity
    // id, or a fresh start after a failure.
    readonly prepareForTakeoff: () => Promise<{ lastRevNumber: number; dbChangedActivityId: number; startedOver: boolean }>;
    readonly setStatusAndRev: (lastRevNumber: number, status?: string) => Promise<void>;
    readonly saveLastRevNumber: (lastRevNumber: number | undefined, status: string) => Promise<void>;
    readonly markFailed: () => Promise<void>;
    readonly startOver: () => Promise<void>;
    // Writes the reminder rows for these events (del by eventId first is the
    // command layer's job; this only puts).
    readonly putReminders: (reminders: readonly Reminder[]) => Promise<void>;
    readonly findRemindersAtShowTime: (showTime: number) => Promise<(Reminder & DbObject)[]>;
    readonly findRemindersAtAutoCloseTime: (autoCloseTime: number) => Promise<(Reminder & DbObject)[]>;
    // Deleted and new/changed calendar events since a rev, as two batched finds
    // (onDBChanged). The reply order is [deleted, newOrChanged].
    readonly findChangedEvents: (lastRevNumber: number) => Promise<{ deleted: (DbObject & { _id?: string })[];
                                                                     changed: (CalendarEvent & DbObject)[] }>;
    // Every reminder row for a set of event ids, batched (onDBChanged).
    readonly findRemindersByEventIds: (eventIds: readonly string[]) => Promise<(Reminder & DbObject)[][]>;
    // Deletes every reminder row for a set of event ids, batched.
    readonly removeRemindersByEventIds: (eventIds: readonly string[]) => Promise<void>;
    readonly getEvents: (ids: readonly string[]) => Promise<(CalendarEvent & DbObject)[]>;
    readonly getReminder: (reminderId: string) => Promise<(Reminder & DbObject) | undefined>;
    readonly snoozeReminder: (reminderId: string, showTime: number) => Promise<void>;
    readonly deleteReminders: (reminderIds: readonly string[]) => Promise<void>;
    // Schedules the next wake. From onWake, startSearchTime is given and past
    // reminders are returned so the caller can still show them.
    readonly findNextWakeTime: (startSearchTime?: number) => Promise<{ pastReminders: (Reminder & DbObject)[] }>;
    readonly findNextAutoCloseTime: () => Promise<void>;
    readonly scheduleDbWatch: (lastRevNumber: number, isUnitTest: boolean) => Promise<void>;
}

const statusQuery = { from: STATUS_KIND };

export const createReminders = (deps: RemindersDeps): Reminders => {
    const { db, bus, log } = deps;

    const nowZeroed = (): number => deps.now();

    const readStatus = async (): Promise<Status | undefined> => {
        const { results } = await db.find<Status>(statusQuery);
        return results[0];
    };

    const mergeStatus = async (props: Payload): Promise<void> => {
        await db.mergeWhere(statusQuery, props);
    };

    // --- status recovery ----------------------------------------------------

    const startOver: Reminders["startOver"] = async () => {
        const status = await readStatus();
        await db.batch([
            { method: "del", params: { query: { from: STATUS_KIND } } },
            { method: "del", params: { query: { from: REMINDER_KIND } } },
        ]);
        for (const id of [status?.wakeActivityId, status?.autoCloseActivityId, status?.dbChangedActivityId]) {
            if (id) {
                await bus.call(`${ACTIVITY_MANAGER}/cancel`, { activityId: id })
                    .catch((error: unknown) => log(`cancel activity ${id} failed: ${String(error)}`));
            }
        }
    };

    const prepareForTakeoff: Reminders["prepareForTakeoff"] = async () => {
        const status = await readStatus().catch(() => undefined);
        if (status && status.status === "OK") {
            return {
                lastRevNumber: status.lastRevNumber ?? 0,
                dbChangedActivityId: status.dbChangedActivityId ?? 0,
                startedOver: false,
            };
        }
        if (status === undefined) {
            // Empty table: first run.
            return { lastRevNumber: 0, dbChangedActivityId: 0, startedOver: false };
        }
        // A status that is not "OK" means we failed last time. Blow it away.
        log("prepareForTakeoff: starting over");
        await startOver();
        return { lastRevNumber: 0, dbChangedActivityId: 0, startedOver: true };
    };

    // Only from onInit: replace the whole status row.
    const setStatusAndRev: Reminders["setStatusAndRev"] = async (lastRevNumber, status = "OK") => {
        await db.batch([
            { method: "del", params: { query: { from: STATUS_KIND } } },
            { method: "put", params: { objects: [{ _kind: STATUS_KIND, lastRevNumber, status }] } },
        ]);
    };

    // Only from onDBChanged: merge the rev forward.
    const saveLastRevNumber: Reminders["saveLastRevNumber"] = async (lastRevNumber, status) => {
        if (lastRevNumber !== undefined) {
            await mergeStatus({ lastRevNumber, status });
        }
    };

    // Any error path merges FAIL, so the next onInit/onDBChanged starts over.
    const markFailed: Reminders["markFailed"] = async () => {
        await mergeStatus({ status: "FAIL" }).catch(() => undefined);
    };

    // --- reminder rows ------------------------------------------------------

    const putReminders: Reminders["putReminders"] = async (reminders) => {
        if (reminders.length > 0) {
            // A Reminder is a db8 object by shape (it carries _kind and only
            // JSON values); it lacks the open index signature DbObject has, so
            // the cast goes through unknown.
            await db.put(reminders as unknown as readonly DbObject[]);
        }
    };

    const findBy = async (prop: "showTime" | "autoCloseTime", val: number) =>
        (await db.find<Reminder & DbObject>({ from: REMINDER_KIND, where: [{ prop, op: "=", val }] })).results;

    const findRemindersAtShowTime: Reminders["findRemindersAtShowTime"] = (showTime) => findBy("showTime", showTime);
    const findRemindersAtAutoCloseTime: Reminders["findRemindersAtAutoCloseTime"] = (autoCloseTime) =>
        findBy("autoCloseTime", autoCloseTime);

    // The two batched event queries onDBChanged runs: deleted since the rev,
    // and new/changed since the rev, each ordered by _rev.
    const findChangedEvents: Reminders["findChangedEvents"] = async (lastRevNumber) => {
        const responses = await db.batch([
            { method: "find", params: { query: { from: EVENT_KIND,
                where: [{ prop: "_del", op: "=", val: true }, { prop: "_rev", op: ">", val: lastRevNumber }],
                orderBy: "_rev" } } },
            { method: "find", params: { query: { from: EVENT_KIND,
                where: [{ prop: "_rev", op: ">", val: lastRevNumber }], orderBy: "_rev" } } },
        ]);
        const resultsOf = (r: Payload | undefined) =>
            (r && Array.isArray((r as { results?: unknown }).results) ? (r as { results: unknown[] }).results : []);
        return {
            deleted: resultsOf(responses[0]) as (DbObject & { _id?: string })[],
            changed: resultsOf(responses[1]) as (CalendarEvent & DbObject)[],
        };
    };

    const findRemindersByEventIds: Reminders["findRemindersByEventIds"] = async (eventIds) => {
        if (eventIds.length === 0) {
            return [];
        }
        const responses = await db.batch(eventIds.map((id) => ({
            method: "find",
            params: { query: { from: REMINDER_KIND, where: [{ prop: "eventId", op: "=", val: id }] } },
        })));
        return responses.map((r) =>
            (r && Array.isArray((r as { results?: unknown }).results)
                ? (r as { results: (Reminder & DbObject)[] }).results : []));
    };

    const removeRemindersByEventIds: Reminders["removeRemindersByEventIds"] = async (eventIds) => {
        if (eventIds.length === 0) {
            return;
        }
        await db.batch(eventIds.map((id) => ({
            method: "del",
            params: { query: { from: REMINDER_KIND, where: [{ prop: "eventId", op: "=", val: id }] } },
        })));
    };

    const getEvents: Reminders["getEvents"] = (ids) =>
        ids.length === 0 ? Promise.resolve([]) : db.get<CalendarEvent & DbObject>(ids);

    const getReminder: Reminders["getReminder"] = async (reminderId) => {
        const { results } = await db.find<Reminder & DbObject>({
            from: REMINDER_KIND, where: [{ prop: "_id", op: "=", val: reminderId }],
        });
        return results[0];
    };

    const snoozeReminder: Reminders["snoozeReminder"] = async (reminderId, showTime) => {
        await db.merge([{ _id: reminderId, showTime }]);
    };

    const deleteReminders: Reminders["deleteReminders"] = async (reminderIds) => {
        if (reminderIds.length > 0) {
            await db.del(reminderIds);
        }
    };

    // --- the three activities -----------------------------------------------
    //
    // Each follows HP's two-step: look at the status row for an existing id,
    // then create (none yet) / complete+restart (reschedule) / complete
    // (teardown), and record the new id. The "created"/"updated"/"completed"/
    // "none"/"failed" status actions drive what the id becomes, exactly as
    // save*ActivityId did.

    // Runs an activitymanager call for a scheduled activity (wake or autoclose),
    // returning the resulting status action and new id. method/params are the
    // activity to run, or null to do nothing.
    const runScheduledActivity = async (
        callbackMethod: string, timeField: "showTime" | "autoCloseTime",
        paramField: "showTime" | "autoCloseTime", activityName: string, description: string,
        reminders: readonly (Reminder & DbObject)[], existingId: number | undefined,
    ): Promise<{ action: StatusAction; id: number }> => {
        const count = reminders.length;
        if (count === 0) {
            if (existingId) {
                // An old activity to complete, nothing new to schedule.
                await bus.call(`${ACTIVITY_MANAGER}/complete`, { activityId: existingId });
                return { action: "completed", id: 0 };
            }
            return { action: "none", id: 0 };
        }
        const date = reminders[0]![timeField];
        const dateString = getUTCDateString(date);
        const callback = { method: `${SELF}/${callbackMethod}`, params: { [paramField]: date } };
        if (existingId) {
            await bus.call(`${ACTIVITY_MANAGER}/complete`, {
                activityId: existingId, restart: true, schedule: { start: dateString }, callback,
            });
            return { action: "updated", id: existingId };
        }
        const reply = await bus.call<{ activityId?: number }>(`${ACTIVITY_MANAGER}/create`, {
            start: true, replace: true,
            activity: {
                name: activityName, description,
                type: { persist: true, foreground: true },
                schedule: { start: dateString }, callback,
            },
        });
        return { action: "created", id: reply.activityId ?? 0 };
    };

    // Records the new activity id for wake/autoclose, as save*ActivityId did:
    // created -> the new id; updated -> unchanged; completed/none/failed -> 0.
    const recordActivityId = async (field: "wakeActivityId" | "autoCloseActivityId",
                                     action: StatusAction, id: number): Promise<void> => {
        if (action === "updated") {
            return; // id unchanged
        }
        await mergeStatus({ [field]: action === "created" ? id : 0 });
    };

    const findNextWakeTime: Reminders["findNextWakeTime"] = async (startSearchTime) => {
        const now = nowZeroed();
        const fromOnWake = startSearchTime !== undefined;
        const searchFrom = startSearchTime ?? now;
        const query = fromOnWake
            ? { from: REMINDER_KIND, where: [{ prop: "showTime", op: ">=", val: searchFrom }], orderBy: "showTime" }
            : { from: REMINDER_KIND, where: [{ prop: "showTime", op: ">=", val: searchFrom }], orderBy: "showTime", limit: 1 };

        let reminders: (Reminder & DbObject)[];
        try {
            reminders = (await db.find<Reminder & DbObject>(query)).results;
        } catch (error) {
            // A db error: do not touch the existing activity or table.
            log(`findNextWakeTime query failed: ${String(error)}`);
            return { pastReminders: [] };
        }

        // From onWake: set aside reminders already in the past (shown now, not
        // rescheduled), and schedule only the next future one.
        let pastReminders: (Reminder & DbObject)[] = [];
        let toSchedule = reminders;
        if (fromOnWake && reminders.length > 0) {
            let i = 0;
            pastReminders = [];
            while (i < reminders.length && reminders[i]!.showTime <= now) {
                pastReminders.push(reminders[i]!);
                i++;
            }
            toSchedule = i < reminders.length ? [reminders[i]!] : [];
        }

        const status = await readStatus().catch(() => undefined);
        try {
            const { action, id } = await runScheduledActivity(
                "onWake", "showTime", "showTime", "calendar.reminders.wake",
                "Time to show a calendar reminder", toSchedule, status?.wakeActivityId);
            await recordActivityId("wakeActivityId", action, id);
        } catch (error) {
            log(`scheduling wake failed: ${String(error)}`);
            await mergeStatus({ wakeActivityId: 0 });
        }
        return { pastReminders };
    };

    const findNextAutoCloseTime: Reminders["findNextAutoCloseTime"] = async () => {
        const now = nowZeroed();
        let reminders: (Reminder & DbObject)[];
        try {
            reminders = (await db.find<Reminder & DbObject>({
                from: REMINDER_KIND, where: [{ prop: "autoCloseTime", op: ">=", val: now }],
                orderBy: "autoCloseTime", limit: 1,
            })).results;
        } catch (error) {
            log(`findNextAutoCloseTime query failed: ${String(error)}`);
            return;
        }
        const status = await readStatus().catch(() => undefined);
        try {
            const { action, id } = await runScheduledActivity(
                "onAutoClose", "autoCloseTime", "autoCloseTime", "calendar.reminders.autoclose",
                "Time to autoclose a calendar reminder", reminders, status?.autoCloseActivityId);
            await recordActivityId("autoCloseActivityId", action, id);
        } catch (error) {
            log(`scheduling autoclose failed: ${String(error)}`);
            await mergeStatus({ autoCloseActivityId: 0 });
        }
    };

    // The persistent db-watch activity: it re-fires onDBChanged whenever a
    // calendar event is written past the last rev we processed.
    const scheduleDbWatch: Reminders["scheduleDbWatch"] = async (lastRevNumber, isUnitTest) => {
        const status = await readStatus().catch(() => undefined);
        const activityId = status?.dbChangedActivityId;
        const eventQuery = {
            from: EVENT_KIND,
            where: [{ prop: "_rev", op: ">", val: lastRevNumber }],
            incDel: true,
        };
        const trigger = { key: "fired", method: "luna://com.palm.db/watch", params: { query: eventQuery } };
        try {
            if (activityId && !isUnitTest) {
                await bus.call(`${ACTIVITY_MANAGER}/complete`, { activityId, restart: true, trigger });
                // complete+restart keeps the same id: no status change.
            } else {
                const reply = await bus.call<{ activityId?: number }>(`${ACTIVITY_MANAGER}/create`, {
                    start: true, replace: true,
                    activity: {
                        name: "calendar.reminders.dbchanged",
                        description: "Calendar database changed, check for new reminders",
                        callback: { method: `${SELF}/onDBChanged`, params: { unittest: isUnitTest } },
                        trigger,
                        type: { immediate: true, priority: "low", persist: true },
                    },
                });
                await mergeStatus({ dbChangedActivityId: reply.activityId ?? 0 });
            }
        } catch (error) {
            log(`scheduling db watch failed: ${String(error)}`);
            await mergeStatus({ dbChangedActivityId: 0 });
        }
    };

    return {
        prepareForTakeoff, setStatusAndRev, saveLastRevNumber, markFailed, startOver,
        putReminders, findRemindersAtShowTime, findRemindersAtAutoCloseTime,
        findChangedEvents, findRemindersByEventIds, removeRemindersByEventIds, getEvents,
        getReminder, snoozeReminder, deleteReminders,
        findNextWakeTime, findNextAutoCloseTime, scheduleDbWatch,
    };
};
