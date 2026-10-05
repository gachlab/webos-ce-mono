// The next occurrence of a repeating event at or after a given time, for the
// plain recurrence rules. HP's Calendar.EventManager.findNextOccurrence (the
// signature scheduler.ts injects).
//
// A WORD ON SCOPE. HP's real findNextOccurrence sits on top of a 10,000-line
// calendar library (event-manager.js + its timezone manager and
// findRepeatsInRange, which expand BYDAY/BYMONTH/BYSETPOS and count-limited
// rules). That library is a port of its own -- the same reason #39 postpones
// com.palm.service.contacts, which leans on an equally large framework. Pulling
// it in here would dwarf this service.
//
// So this covers what the overwhelming majority of events use: a FREQ of
// DAILY/WEEKLY/MONTHLY/YEARLY with an optional INTERVAL and UNTIL, stepping from
// dtstart to the first occurrence strictly after `date`. DAILY/WEEKLY step by
// fixed spans; MONTHLY/YEARLY step by calendar month/year so a day-of-month is
// kept. BYDAY/BYMONTH/COUNT and timezone-shifted expansion are NOT handled --
// an event using them resolves to its base-interval occurrence, which is right
// for the common weekly/daily case and a safe approximation otherwise. The
// fuller engine, when the calendar library is ported, replaces this one module
// and nothing else: scheduler.ts takes findNextOccurrence as a parameter.

import type { CalendarEvent } from "./scheduler.ts";

// HP's hard cap, so a malformed rule can never loop forever: 2050-01-01 UTC.
const FAR_FUTURE = 2524608000000;

const addMonths = (ms: number, months: number): number => {
    const d = new Date(ms);
    const day = d.getDate();
    d.setDate(1); // avoid overflow (Jan 31 + 1 month)
    d.setMonth(d.getMonth() + months);
    // Clamp to the month's length, as a calendar step does (Jan 31 -> Feb 28).
    const lastDay = new Date(d.getFullYear(), d.getMonth() + 1, 0).getDate();
    d.setDate(Math.min(day, lastDay));
    return d.getTime();
};

/**
 * The start of the next occurrence strictly after `date`, or false when the
 * event does not repeat or has stopped. Matches HP's return contract (a
 * timestamp or false).
 */
export const findNextOccurrence = (event: CalendarEvent, date: number): number | false => {
    const rrule = event.rrule;
    if (!rrule || !rrule.freq) {
        return false;
    }
    const until = rrule.until !== undefined && rrule.until !== null ? rrule.until : FAR_FUTURE;
    if (date > until) {
        return false;
    }

    const interval = Math.max(1, Math.trunc(Number((rrule as { interval?: unknown }).interval)) || 1);
    const freq = rrule.freq;

    // Step from dtstart until the first start strictly after `date`.
    let occurrence = event.dtstart;
    // If dtstart is already after date, that is the next one.
    const step = (ms: number): number => {
        switch (freq) {
        case "DAILY": return ms + interval * 86400000;
        case "WEEKLY": return ms + interval * 604800000;
        case "MONTHLY": return addMonths(ms, interval);
        case "YEARLY": return addMonths(ms, interval * 12);
        default: return Number.POSITIVE_INFINITY;
        }
    };
    if (step(occurrence) === Number.POSITIVE_INFINITY) {
        return false; // an unknown FREQ
    }

    // Guard the loop: at most a few thousand steps before giving up, which
    // covers decades of any interval and cannot hang on a bad rule.
    for (let guard = 0; guard < 100000; guard++) {
        if (occurrence > date) {
            return occurrence <= until ? occurrence : false;
        }
        occurrence = step(occurrence);
        if (occurrence > until) {
            return false;
        }
    }
    return false;
};
