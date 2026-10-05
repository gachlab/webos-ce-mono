// The reminder scheduler's decisions, free of the bus and of db8, rewritten
// from reminder-assistant.js's findDisplayAlarm and findReminderTimes (the
// loop, lines ~103-241). This is the hot path the whole service turns on: for
// each calendar event with a display alarm, when its reminder should show
// (showTime), when it should auto-close (autoCloseTime), and whether it is near
// enough to matter at all.
//
// Kept here, bus-free, so test/calendar-reminders.unit.test.ts can pin every
// decision against reminder-assistant.js's own results without db8 or
// activitymanager. The one thing it cannot compute itself is the next
// occurrence of a repeating event -- HP's Calendar.EventManager.findNextOccurrence,
// a library of its own -- so that is injected, along with `now`, which the loop
// reads once and a test must control.

import { type Alarm, calculateAlarmTime } from "./datetime.ts";

// A calendar event as com.palm.calendarevent:1 stores it. Only the fields the
// scheduler reads are named; db8 carries the rest.
export interface CalendarEvent {
    readonly _id?: string;
    readonly _rev?: number;
    readonly subject?: string;
    readonly location?: string;
    readonly allDay?: boolean;
    readonly attendees?: unknown;
    readonly calendarId?: string;
    readonly dtstart: number;
    readonly dtend: number;
    readonly alarm?: readonly Alarm[];
    readonly rrule?: { readonly freq?: string; readonly until?: number } | undefined;
}

// A reminder row, exactly the shape findReminderTimes pushed (and the db8 kind
// com.palm.service.calendar.reminders:1 stores). The property names are the
// contract: HP's calendar app reads them back.
export interface Reminder {
    readonly _kind: "com.palm.service.calendar.reminders:1";
    readonly eventId: string | undefined;
    readonly subject: string | undefined;
    readonly location: string | undefined;
    readonly isAllDay: boolean | undefined;
    readonly attendees: unknown;
    readonly calendarId: string | undefined;
    readonly startTime: number;
    readonly endTime: number;
    readonly alarmTime: number;
    readonly showTime: number;
    readonly autoCloseTime: number;
    readonly isRepeating: boolean;
}

export const REMINDER_KIND = "com.palm.service.calendar.reminders:1";

// Minimum time a reminder stays open: 15 minutes. HP's magic number.
export const MIN_AUTOCLOSE_MS = 900000;

/**
 * The index of the event's display alarm, or null. findDisplayAlarm.
 *
 * HP's rule, preserved exactly including its quirk: the first alarm whose
 * action is absent or "display" (case-insensitive) AND whose trigger is a
 * DURATION with a value other than "none". A DATE-TIME display alarm falls
 * through to null here -- the loop below never schedules one -- which is HP's
 * own behavior and so the contract, not a bug to fix in this port.
 */
export const findDisplayAlarm = (alarms: readonly Alarm[] | undefined): number | null => {
    const count = alarms?.length ?? 0;
    for (let i = 0; i < count; i++) {
        const alarm = alarms![i]!;
        if (alarm.action && alarm.action.toLowerCase() !== "display") {
            continue;
        }
        const trigger = alarm.alarmTrigger;
        if (trigger && trigger.valueType === "DURATION" && trigger.value
            && trigger.value.toLowerCase() !== "none") {
            return i;
        }
    }
    return null;
};

export interface FindRemindersInput {
    readonly events: readonly CalendarEvent[];
    // The last db8 _rev already processed; the result carries it forward,
    // raised to the highest _rev seen.
    readonly lastRevNumber?: number;
    // onDismiss's two flags: a dismissed occurrence must not refire at its own
    // start time, so `now` is nudged past it and non-repeating events are
    // skipped.
    readonly eventDismissed?: boolean;
    readonly skipPastStartTime?: number;
}

export interface FindRemindersResult {
    readonly reminderList: Reminder[];
    readonly lastRevNumber: number;
}

// The next occurrence of a repeating event that starts at or after `now`, or
// false when there is none. HP's Calendar.EventManager.findNextOccurrence.
export type FindNextOccurrence = (event: CalendarEvent, now: number) => number | false;

/**
 * The reminders that should exist for these events, and the highest _rev seen.
 * findReminderTimes, minus the db8 and future plumbing.
 *
 * `now` is passed in (the loop read it once); `findNextOccurrence` is the
 * injected rrule engine. Everything else is HP's logic line for line.
 */
export const findReminders = (
    input: FindRemindersInput,
    now: number,
    findNextOccurrence: FindNextOccurrence,
): FindRemindersResult => {
    const { events, eventDismissed, skipPastStartTime } = input;
    let revNumber = input.lastRevNumber ?? 0;

    // A dismissed occurrence: look only past its own start, so it does not
    // immediately refire.
    const effectiveNow = eventDismissed && skipPastStartTime !== undefined
        ? skipPastStartTime + 1000
        : now;

    const reminderList: Reminder[] = [];

    for (const event of events) {
        if (event._rev !== undefined && event._rev > revNumber) {
            revNumber = event._rev;
        }

        if (!event.alarm || event.alarm.length === 0) {
            continue;
        }

        const displayAlarmIndex = findDisplayAlarm(event.alarm);
        if (displayAlarmIndex === null) {
            continue;
        }

        const rrule = event.rrule;
        const repeats = Boolean(rrule && rrule.freq);

        // The event is over and does not repeat.
        if (event.dtend < effectiveNow && !rrule) {
            continue;
        }
        // A dismissed, non-repeating event does not come back.
        if (eventDismissed && !rrule) {
            continue;
        }
        // The repeat has ended.
        if (repeats && rrule!.until !== undefined && rrule!.until < effectiveNow) {
            continue;
        }

        let startTime: number;
        let isRepeatEvent: boolean;
        // A live repeat: its next occurrence starting at or after now.
        if (repeats && (rrule!.until === undefined || rrule!.until > effectiveNow)) {
            const next = findNextOccurrence(event, effectiveNow);
            if (next === false) {
                continue;
            }
            startTime = next;
            isRepeatEvent = true;
        } else {
            startTime = event.dtstart;
            isRepeatEvent = false;
        }

        const alarm = event.alarm[displayAlarmIndex]!;
        const alarmTime = calculateAlarmTime(startTime, alarm);
        // The lead-time alarm already passed but the event itself is still
        // future: show at the start instead.
        const showTime = alarmTime < effectiveNow && startTime >= effectiveNow ? startTime : alarmTime;

        const duration = event.dtend - event.dtstart;
        const autoCloseTime = duration < MIN_AUTOCLOSE_MS ? startTime + MIN_AUTOCLOSE_MS : startTime + duration;

        const shouldBeShowingNow = !eventDismissed && effectiveNow >= showTime && effectiveNow <= autoCloseTime;

        if (showTime >= effectiveNow || shouldBeShowingNow) {
            reminderList.push({
                _kind: REMINDER_KIND,
                eventId: event._id,
                subject: event.subject,
                location: event.location,
                isAllDay: event.allDay,
                attendees: event.attendees,
                calendarId: event.calendarId,
                startTime,
                endTime: startTime + duration,
                alarmTime,
                showTime,
                autoCloseTime,
                isRepeating: isRepeatEvent,
            });
        }
    }

    return { reminderList, lastRevNumber: revNumber };
};
