com.palm.service.calendar.reminders
===================================

Calendar reminders — scheduling, showing and auto-closing — rewritten from HP's
JavaScript service onto the node-services kit (#39), keeping its API, its db8
kinds and its activity-manager behaviour exactly.

HP's service (`components/app-services/com.palm.service.calendar.reminders`) ran
on the node 0.4 stack: MojoLoader, `mojoservice` assistants returning
`Foundations.Control.Future`s, and `datejs` grafted onto `Date`. This is the
same service on modern node, type-stripped `.ts`, promises and async/await, and
no `datejs`.

What it does
------------

Nothing calls it from a UI; it is driven by activities and a db watch:

| Method | Triggered by | What it does |
|---|---|---|
| `onInit` | boot | builds the reminder table from the calendar events, schedules the next wake, the next auto-close and the db watch |
| `onWake` | the `calendar.reminders.wake` activity | opens the calendar app to show the reminders due at a `showTime`, reschedules |
| `onAutoClose` | the `calendar.reminders.autoclose` activity | tells the app to close the reminders at an `autoCloseTime`, reschedules |
| `onDBChanged` | the `calendar.reminders.dbchanged` db-watch activity | reconciles the table against the events that changed, tells the app what to update or close |
| `onSnooze` | the calendar app | moves a reminder's `showTime` forward (default 5 min) |
| `onDismiss` | the calendar app | removes a reminder and makes sure a dismissed occurrence does not refire at its own start |

The method names, their payloads, the replies, the two db8 kinds and the four
`com.palm.app.calendar` launch params (`alarm`, `alarmClose`, `alarmDeleted`,
`alarmUpdated`) are HP's; a changed one is a reminder the calendar app no longer
shows.

How it is laid out
------------------

Split so the delicate parts are testable without a bus, a db8 or a clock:

* **`datetime.ts`** — the date arithmetic off `datejs`: `parseDateTime` (the
  15-char local and 16-char UTC forms), `parseDuration` (RFC5545 `-PT15M`),
  `calculateAlarmTime` and `getUTCDateString`. The only non-portable logic, held
  to `datejs`'s own results by a golden test.
* **`scheduler.ts`** — `findDisplayAlarm` and `findReminders`: for each event
  with a display alarm, its `showTime`, its `autoCloseTime` (a 15-minute floor),
  and whether it is near enough to schedule. Pure; `now` and the recurrence
  engine are passed in.
* **`reminders.ts`** — the db8 writes and the activity-manager scheduling: the
  status singleton, the wake/auto-close/db-watch activities (create vs
  complete-and-restart), and the failure recovery that makes the next run start
  over.
* **`commands.ts`** — the six commands, orchestrating the two layers and the
  calendar app.
* **`recurrence.ts`** — `findNextOccurrence` for the plain rules (see below).
* **`service.ts` / `main.ts`** — the wiring and the node entry point.

Recurrence: what is and is not handled
--------------------------------------

HP's `findNextOccurrence` sits on a ~10,000-line calendar library
(`event-manager.js` and its timezone manager) that expands `BYDAY`/`BYMONTH`/
`BYSETPOS` and count-limited rules. That library is a port of its own — the same
reason #39 postpones `com.palm.service.contacts` — so `recurrence.ts` covers the
rules the overwhelming majority of events use: a `FREQ` of
`DAILY`/`WEEKLY`/`MONTHLY`/`YEARLY` with an optional `INTERVAL` and `UNTIL`.
`BYDAY`/`BYMONTH`/`COUNT` and timezone-shifted expansion are not handled; an
event using them falls back to its base-interval occurrence. `scheduler.ts`
takes the recurrence engine as a parameter, so a future calendar-library port
replaces this one module and nothing else.

A db8 permission it depends on
------------------------------

The service reads `com.palm.calendarevent:1`, which the calendar app owns. The
grant that lets it is HP's own, in
`components/core-apps/com.palm.app.calendar/configuration/db/permissions/com.palm.calendarevent`
(it already names `com.palm.service.calendar.reminders`), and `assemble-rootfs`
installs it. Nothing new is needed here, but the hub test sets the same grant so
the real db8 answers the service's `find` as it does on device.

Testing it
----------

```sh
services/node-services/test/run.sh services/node-services/test/calendar-reminders.unit.test.ts
services/node-services/test/run.sh services/node-services/test/calendar-reminders.hub.test.ts
```

* **`calendar-reminders.unit.test.ts`** — `datetime.ts` is diffed against HP's
  `datejs` + `utils.js` loaded in a sandbox, and `scheduler.ts` against a
  transcription of HP's `findReminderTimes`, so the port is held to the old
  results. `recurrence.ts` and all six commands run over an in-memory db8 and a
  recording bus.
* **`calendar-reminders.hub.test.ts`** — the service against a real
  `mojodb-luna`: the kinds register, the rows come back through db8's own
  `showTime`/`eventId` indexes, and the status singleton round-trips.

Verified by mutation: removing the `onWake` duplicate filter, the `showTime`
tie-break, the 15-minute auto-close floor, the UTC path in `parseDateTime`, or
the reminder write in `onInit` each make a test fail.
