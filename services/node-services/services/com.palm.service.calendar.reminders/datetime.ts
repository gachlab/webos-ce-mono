// The date arithmetic com.palm.service.calendar.reminders needs, off HP's
// datejs. Rewritten from utils.js (utilParseDateTime, utilParseDuration,
// utilCalculateAlarmTime, utilGetUTCDateString), which grafted datejs onto
// Date (parseExact/addMinutes/add/addWeeks/getTimezoneOffset) in prologue.js.
//
// This is the only non-portable logic in the port, so it is kept here, free of
// the bus and of db8, and held to datejs's own results by golden tests
// (test/calendar-reminders.unit.test.ts). The reminder rows the service writes
// to db8 carry these timestamps, and HP's calendar app reads them back, so a
// drift here is a reminder that fires at the wrong minute.
//
// No classes and no datejs: plain functions over the standard Date, matching
// the kit's functional style.

// A calendar alarm, as it appears on a com.palm.calendarevent:1. Only the
// trigger matters to the time math; the action/type live in the scheduler.
export interface AlarmTrigger {
    readonly valueType?: string; // "DATE-TIME" | "DURATION"
    readonly value?: string;
}

export interface Alarm {
    readonly alarmTrigger?: AlarmTrigger;
    readonly action?: string;
}

const pad2 = (n: number): string => (n > 9 ? String(n) : "0" + n);

// A "yyyyMMddTHHmmss" string to its parts. Returns undefined when the string is
// not 15 digits-with-T in the right places, which utilParseDateTime treated as
// a malformed alarm (0).
const parseStamp = (s: string): { year: number; month: number; day: number;
                                   hour: number; minute: number; second: number } | undefined => {
    // datejs's parseExact("yyyyMMddTHHmmss") accepts exactly YYYYMMDDTHHMMSS.
    const match = /^(\d{4})(\d{2})(\d{2})T(\d{2})(\d{2})(\d{2})$/.exec(s);
    if (!match) {
        return undefined;
    }
    return {
        year: Number(match[1]), month: Number(match[2]), day: Number(match[3]),
        hour: Number(match[4]), minute: Number(match[5]), second: Number(match[6]),
    };
};

/**
 * A date string to an epoch-ms timestamp. utilParseDateTime.
 *
 * Two forms, by length, exactly as HP's switch did:
 *  - 15 chars "YYYYMMDDTHHMMSS": local wall-clock time. datejs's parseExact
 *    built a Date in the local zone, and getTime() is that instant.
 *  - 16 chars "YYYYMMDDTHHMMSSZ": UTC wall-clock time. HP trimmed the Z, parsed
 *    the 15 as local, then addMinutes(-getTimezoneOffset()) to shift the
 *    wall-clock onto UTC. Date.UTC does the same thing without the dance.
 *  - anything else: 0 (malformed).
 */
export const parseDateTime = (alarmString: string): number => {
    if (alarmString.length === 15) {
        const p = parseStamp(alarmString);
        if (!p) {
            return 0;
        }
        // Local time, as datejs's parseExact produced.
        return new Date(p.year, p.month - 1, p.day, p.hour, p.minute, p.second, 0).getTime();
    }
    if (alarmString.length === 16) {
        const p = parseStamp(alarmString.substr(0, 15));
        if (!p) {
            return 0;
        }
        // UTC wall-clock: the same digits read as UTC rather than local.
        return Date.UTC(p.year, p.month - 1, p.day, p.hour, p.minute, p.second, 0);
    }
    return 0;
};

/**
 * An RFC5545 duration applied to a start time, as an epoch-ms timestamp.
 * utilParseDuration ("-PT15M" etc.).
 *
 * The sign is taken from a leading "-", then weeks/days/hours/minutes/seconds
 * are applied to startTime. HP ran this through datejs on a LOCAL Date:
 * add({days}) and addWeeks step the calendar day (setDate), which preserves the
 * wall-clock across a DST change, while add({hours,minutes,seconds}) is plain
 * millisecond arithmetic. So weeks and days are applied with setDate here and
 * hours/minutes/seconds as fixed ms -- a multi-day trigger spanning a DST
 * boundary lands on the same wall-clock time HP's did, an hour off from naive
 * fixed-ms. A string that matches nothing is 0, as HP's returned.
 */
export const parseDuration = (alarmString: string, startTime: number): number => {
    const negative = alarmString[0] === "-" ? -1 : 1;
    // The same grammar HP used: P[nW|nD]T[nH][nM][nS], each unit optional.
    const tokens = /P([0-9]{1,3}[WD])*T*([0-9]{1,3}H)*([0-9]{1,3}M)*([0-9]{1,3}S)*/.exec(alarmString);
    if (!tokens) {
        return 0;
    }
    let weeks = 0, days = 0, hours = 0, minutes = 0, seconds = 0;
    for (const token of tokens) {
        if (token === undefined) {
            continue;
        }
        const unit = token[token.length - 1];
        const number = parseInt(token.slice(0, -1), 10) * negative;
        switch (unit) {
        case "W": weeks = number; break;
        case "D": days = number; break;
        case "H": hours = number; break;
        case "M": minutes = number; break;
        case "S": seconds = number; break;
        default: break;
        }
    }
    // Weeks and days: calendar steps on a local Date (datejs addWeeks / add
    // days), wall-clock preserving across DST.
    const date = new Date(startTime);
    date.setDate(date.getDate() + weeks * 7 + days);
    // Hours/minutes/seconds: fixed-length, as datejs's add did.
    return date.getTime() + ((hours * 60 + minutes) * 60 + seconds) * 1000;
};

/**
 * When an alarm should fire, from the event's start time and the alarm.
 * utilCalculateAlarmTime: a DATE-TIME value is an absolute time, a DURATION is
 * relative to startTime (the default branch). A malformed or missing alarm is
 * 0, as HP returned.
 */
export const calculateAlarmTime = (startTime: number, alarm: Alarm | undefined): number => {
    const trigger = alarm?.alarmTrigger;
    if (!trigger || !trigger.valueType || !trigger.value) {
        return 0;
    }
    if (trigger.valueType === "DATE-TIME") {
        return parseDateTime(trigger.value);
    }
    // DURATION, and HP's default for any other valueType.
    return parseDuration(trigger.value, startTime);
};

/**
 * A timestamp as "YYYY-MM-DD HH:MM:SSZ" in UTC. utilGetUTCDateString, which is
 * what the activity manager's schedule.start wants.
 */
export const getUTCDateString = (timestamp: number): string => {
    const date = new Date(timestamp);
    return `${date.getUTCFullYear()}-${pad2(date.getUTCMonth() + 1)}-${pad2(date.getUTCDate())} `
         + `${pad2(date.getUTCHours())}:${pad2(date.getUTCMinutes())}:${pad2(date.getUTCSeconds())}Z`;
};
