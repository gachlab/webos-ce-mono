// The six commands of com.palm.service.calendar.reminders, rewritten from HP's
// on-*-handler.js. Each keeps its method name, parameters, replies and side
// effects -- the db8 writes, the activity scheduling, and the
// applicationManager/open payloads the calendar app reads to show, update and
// close reminders.
//
// The commands are triggered by activities and db watches, not by UI: onWake
// and onAutoClose fire from scheduled activities, onDBChanged from a db-watch
// activity, and onInit at boot; onSnooze and onDismiss are the only ones the
// app calls directly. scheduler.ts decides which reminders exist; reminders.ts
// writes them and schedules the activities; this layer orchestrates the two and
// talks to the app.

import type { Bus, Payload } from "#kit/luna.ts";
import { type Command, mojoHandler } from "#kit/mojoservice.ts";
import { type CalendarEvent, findReminders, type FindNextOccurrence, type Reminder } from "./scheduler.ts";
import type { DbObject } from "#kit/db8.ts";
import type { Reminders } from "./reminders.ts";

const SELF = "luna://com.palm.service.calendar.reminders";
const APP_MANAGER = "luna://com.palm.applicationManager";
const CALENDAR_APP = "com.palm.app.calendar";
const DEFAULT_SNOOZE_MS = 300000; // 5 minutes

export interface CommandDeps {
    readonly bus: Pick<Bus, "call">;
    readonly reminders: Reminders;
    // All calendar events, ordered by _rev (onInit's full scan).
    readonly allEvents: () => Promise<(CalendarEvent & DbObject)[]>;
    // The rrule engine scheduler.ts needs; injected because it is a library of
    // its own (HP's Calendar.EventManager.findNextOccurrence).
    readonly findNextOccurrence: FindNextOccurrence;
    readonly now: () => number;
    readonly log: (message: string) => void;
}

type Args = Payload;

// Fire-and-forget launch of the calendar app with a params payload. HP never
// waited on these (the app shows/closes reminders out of band).
const openApp = (bus: Pick<Bus, "call">, params: Payload, log: (m: string) => void): void => {
    void bus.call(`${APP_MANAGER}/open`, { id: CALENDAR_APP, params })
        .catch((error: unknown) => log(`opening the calendar app failed: ${String(error)}`));
};

// Timestamps are stringified before crossing the bus, so the app does not
// truncate them to 32-bit ints. Returns a copy; the stored row is untouched.
const stringifyTimes = (reminder: Reminder & DbObject): Payload => ({
    ...reminder,
    startTime: String(reminder.startTime),
    endTime: String(reminder.endTime),
    showTime: String(reminder.showTime),
    autoCloseTime: String(reminder.autoCloseTime),
});

export const createCommands = (deps: CommandDeps): Command[] => {
    const { reminders, bus, log } = deps;

    // scheduler.ts over a set of events, then write the rows. The highest rev
    // seen is returned so onInit/onDBChanged can record it.
    const scheduleFor = async (events: readonly (CalendarEvent & DbObject)[],
                               lastRevNumber: number,
                               flags: { eventDismissed?: boolean; skipPastStartTime?: number } = {}): Promise<number> => {
        const result = findReminders(
            { events, lastRevNumber, ...flags }, deps.now(), deps.findNextOccurrence);
        await reminders.putReminders(result.reminderList);
        return result.lastRevNumber;
    };

    // ---- onInit --------------------------------------------------------------

    const doFullInit = async (unittest: boolean): Promise<Payload> => {
        const events = await deps.allEvents().catch(() => [] as (CalendarEvent & DbObject)[]);
        const lastRevNumber = events.length ? (events[events.length - 1]!._rev ?? 0) : 0;
        const highestRev = await scheduleFor(events, lastRevNumber);
        try {
            await reminders.setStatusAndRev(highestRev);
        } catch (error) {
            log(`init: could not save status: ${String(error)}`);
            await reminders.markFailed();
        }
        await reminders.findNextWakeTime();
        await reminders.findNextAutoCloseTime();
        await reminders.scheduleDbWatch(highestRev, unittest);
        return { returnValue: true };
    };

    const onInit = mojoHandler<Args>(async (request) => {
        const unittest = Boolean(request.payload.unittest);
        const takeoff = await reminders.prepareForTakeoff();
        // A surviving db-watch activity means we were already running; let the
        // db-changed path reconcile instead of a full rescan.
        if (takeoff.dbChangedActivityId) {
            void bus.call(`${SELF}/onDBChanged`, { unittest })
                .catch((error: unknown) => log(`onInit -> onDBChanged failed: ${String(error)}`));
            return { returnValue: true, startedOver: false };
        }
        return doFullInit(unittest);
    });

    // ---- onWake --------------------------------------------------------------

    const onWake = mojoHandler<Args>(async (request) => {
        const showTime = request.payload.showTime;
        if (showTime === undefined) {
            throw `Missing args! Need showTime.  Received: ${JSON.stringify(request.payload)}`;
        }
        const at = await reminders.findRemindersAtShowTime(Number(showTime));
        // Skip one second ahead so the just-fired reminder is not re-found; the
        // scheduler returns any others already in the past to show as well.
        const { pastReminders } = await reminders.findNextWakeTime(Number(showTime) + 1000);
        const toShow = [...at, ...pastReminders];

        // NOV-102491: duplicate rows have been seen; show each eventId+startTime
        // once.
        const seen = new Set<string>();
        const remindersToShow: Payload[] = [];
        for (const reminder of toShow) {
            const key = `${reminder.eventId}${reminder.startTime}`;
            if (seen.has(key)) {
                continue;
            }
            seen.add(key);
            remindersToShow.push(stringifyTimes(reminder));
        }
        if (remindersToShow.length > 0) {
            openApp(bus, { alarm: remindersToShow }, log);
        }
        return { returnValue: true };
    });

    // ---- onSnooze ------------------------------------------------------------

    const onSnooze = mojoHandler<Args>(async (request) => {
        const { reminderId, snoozeDuration } = request.payload;
        if (!reminderId) {
            throw `Missing args! Need reminderId.  Received: ${JSON.stringify(request.payload)}`;
        }
        const duration = typeof snoozeDuration === "number" && snoozeDuration ? snoozeDuration : DEFAULT_SNOOZE_MS;
        // now with the milliseconds zeroed, as HP did.
        const snoozeUntil = deps.now() + duration;

        const existing = await reminders.getReminder(String(reminderId));
        if (existing) {
            try {
                await reminders.snoozeReminder(String(reminderId), snoozeUntil);
            } catch (error) {
                log(`snooze merge failed for ${String(reminderId)}: ${String(error)}`);
                await reminders.markFailed();
            }
        } else {
            log(`snooze: reminder ${String(reminderId)} does not exist`);
        }
        await reminders.findNextWakeTime();
        await reminders.findNextAutoCloseTime();
        return { returnValue: true };
    });

    // ---- onDismiss -----------------------------------------------------------

    const onDismiss = mojoHandler<Args>(async (request) => {
        const { reminderId, eventId, startTime } = request.payload;
        if (reminderId === undefined || eventId === undefined || startTime === undefined) {
            throw `Missing args! Need reminderId, eventId, and startTime.  Received: ${JSON.stringify(request.payload)}`;
        }
        try {
            await reminders.deleteReminders([String(reminderId)]);
        } catch (error) {
            log(`dismiss: could not delete ${String(reminderId)}: ${String(error)}`);
            await reminders.markFailed();
        }
        // Feed the event back through, told to look only past this occurrence's
        // start so it does not refire at its own start time.
        const events = await reminders.getEvents([String(eventId)]);
        await scheduleFor(events, 0, { eventDismissed: true, skipPastStartTime: Number(startTime) });
        await reminders.findNextWakeTime();
        await reminders.findNextAutoCloseTime();
        return { returnValue: true };
    });

    // ---- onAutoClose ---------------------------------------------------------

    const onAutoClose = mojoHandler<Args>(async (request) => {
        const autoCloseTime = request.payload.autoCloseTime;
        if (!autoCloseTime) {
            throw `Missing args! Need autoCloseTime.  Received: ${JSON.stringify(request.payload)}`;
        }
        const toClose = await reminders.findRemindersAtAutoCloseTime(Number(autoCloseTime))
            .catch(() => [] as (Reminder & DbObject)[]);
        const eventIds = toClose.map((r) => r.eventId).filter((id): id is string => id !== undefined);
        const reminderIds = toClose.map((r) => r._id).filter((id): id is string => id !== undefined);

        if (reminderIds.length > 0) {
            openApp(bus, { alarmClose: reminderIds }, log);
            try {
                await reminders.deleteReminders(reminderIds);
            } catch (error) {
                log(`autoclose: could not delete reminders: ${String(error)}`);
                await reminders.markFailed();
            }
        }
        // Reschedule the events we just closed (a repeat has a next occurrence).
        const events = await reminders.getEvents(eventIds);
        await scheduleFor(events, 0);
        await reminders.findNextWakeTime();
        await reminders.findNextAutoCloseTime();
        return { returnValue: true };
    });

    // ---- onDBChanged ---------------------------------------------------------

    const onDBChanged = mojoHandler<Args>(async (request) => {
        if (request.payload.unittest) {
            return { returnValue: true };
        }
        const takeoff = await reminders.prepareForTakeoff();
        let lastRevNumber = takeoff.lastRevNumber;

        const { deleted, changed } = await reminders.findChangedEvents(lastRevNumber)
            .catch(() => ({ deleted: [], changed: [] as (CalendarEvent & DbObject)[] }));

        // The highest rev across both sets; with no changes, nudge forward one
        // so an empty/spurious fire does not spin on the same watch (HP returned
        // lastRevNumber+1 and kept going -- it does NOT short-circuit, so the
        // wake and autoclose activities are still rescheduled below).
        const hasChanges = deleted.length > 0 || changed.length > 0;
        const highest = (items: readonly DbObject[]) =>
            items.reduce((max, e) => (e._rev !== undefined && e._rev > max ? e._rev : max), 0);
        lastRevNumber = hasChanges ? Math.max(highest(deleted), highest(changed), lastRevNumber) : lastRevNumber + 1;

        const deletedEventIds = deleted.map((e) => e._id).filter((id): id is string => id !== undefined);
        const allEventIds = [...deletedEventIds,
            ...changed.map((e) => e._id).filter((id): id is string => id !== undefined)];
        // Events that still exist, have an alarm, and so may need rescheduling.
        const newOrChangedEventIds = changed
            .filter((e) => e._del !== true && e.alarm && e.alarm.length > 0 && e.alarm[0]!.alarmTrigger)
            .map((e) => e._id).filter((id): id is string => id !== undefined);

        if (deletedEventIds.length > 0) {
            openApp(bus, { alarmDeleted: deletedEventIds }, log);
        }

        // Clear every affected reminder, then re-add the ones that still apply.
        let status = "OK";
        if (allEventIds.length > 0) {
            try {
                await reminders.removeRemindersByEventIds(allEventIds);
            } catch (error) {
                log(`onDBChanged: could not remove reminders: ${String(error)}`);
                status = "FAIL";
                await reminders.markFailed();
            }
        }
        const events = await reminders.getEvents(newOrChangedEventIds);
        const highestRev = await scheduleFor(events, lastRevNumber);
        await reminders.saveLastRevNumber(highestRev, status).catch(async () => {
            log("onDBChanged: could not save rev");
            await reminders.markFailed();
        });

        // Tell the app which live reminders to update or close.
        await updateLiveReminders(newOrChangedEventIds);

        await reminders.findNextWakeTime();
        await reminders.findNextAutoCloseTime();
        await reminders.scheduleDbWatch(lastRevNumber, false);
        return { returnValue: true };
    });

    // A changed event's live reminder is replaced (still showing) or closed (no
    // longer current). onDBChanged's updateLiveReminders.
    const updateLiveReminders = async (eventIds: readonly string[]): Promise<void> => {
        if (eventIds.length === 0) {
            return;
        }
        const perEvent = await reminders.findRemindersByEventIds(eventIds).catch(() => []);
        if (perEvent.length === 0) {
            return;
        }
        const now = deps.now();
        const remindersToUpdate: Payload[] = [];
        const eventIdsToClose: string[] = [];
        for (let i = 0; i < perEvent.length; i++) {
            const results = perEvent[i] ?? [];
            if (results.length === 0) {
                // No reminder was made for this event: close whatever is live.
                if (eventIds[i] !== undefined) {
                    eventIdsToClose.push(eventIds[i]!);
                }
                continue;
            }
            for (const reminder of results) {
                if (now >= reminder.alarmTime && now <= reminder.autoCloseTime) {
                    remindersToUpdate.push(stringifyTimes(reminder));
                } else if (eventIds[i] !== undefined) {
                    eventIdsToClose.push(eventIds[i]!);
                }
            }
        }
        if (remindersToUpdate.length > 0 || eventIdsToClose.length > 0) {
            openApp(bus, { alarmUpdated: { update: remindersToUpdate, close: eventIdsToClose } }, log);
        }
    };

    return [
        { name: "onInit", handler: onInit },
        { name: "onWake", handler: onWake },
        { name: "onDBChanged", handler: onDBChanged },
        { name: "onSnooze", handler: onSnooze },
        { name: "onDismiss", handler: onDismiss },
        { name: "onAutoClose", handler: onAutoClose },
    ];
};
