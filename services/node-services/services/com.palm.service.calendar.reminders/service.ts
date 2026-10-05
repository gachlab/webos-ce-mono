// com.palm.service.calendar.reminders: wiring. Rewritten from HP's
// reminder-assistant.js + the six on-*-handler.js, on the node-services kit.
//
// The service is started on demand by the hub when one of its methods is
// called (an activity firing onWake/onAutoClose/onDBChanged, or the calendar
// app calling onSnooze/onDismiss, or boot calling onInit). It registers its two
// db8 kinds, wires the command layer over db8 and the bus, and exits once idle;
// the scheduling that outlives it lives in activity-manager activities, not in
// this process.

import { createDb8 } from "#kit/db8.ts";
import type { Bus, BusOptions, Handler } from "#kit/luna.ts";
import { DEFAULT_COMMAND_TIMEOUT, DEFAULT_IDLE_MS, QUIT_DELAY_MS } from "#kit/mojoservice.ts";
import { createCommands } from "./commands.ts";
import { findNextOccurrence as defaultFindNextOccurrence } from "./recurrence.ts";
import { createReminders, EVENT_KIND, STATUS_KIND } from "./reminders.ts";
import { REMINDER_KIND, type CalendarEvent, type FindNextOccurrence } from "./scheduler.ts";
import type { DbObject } from "#kit/db8.ts";

export const SERVICE_NAME = "com.palm.service.calendar.reminders";

// The two db8 kinds this service owns, as db/kinds/* declared them. Registered
// on start so a fresh db8 has them before the first find.
const KINDS = [
    {
        id: REMINDER_KIND,
        owner: SERVICE_NAME,
        indexes: [
            { name: "showTime", props: [{ name: "showTime" }] },
            { name: "eventId", props: [{ name: "eventId" }] },
            { name: "autoCloseTime", props: [{ name: "autoCloseTime" }] },
        ],
    },
    { id: STATUS_KIND, owner: SERVICE_NAME, indexes: [] },
];

export interface ReminderServiceDeps {
    readonly openBus: (name: string, options: BusOptions) => Bus;
    readonly createActivity: () => NonNullable<BusOptions["activity"]>;
    // Injected so a test controls time; defaults to now with the milliseconds
    // zeroed, as HP's subroutines did.
    readonly now?: () => number;
    // Injected so a test (and a future calendar-lib port) can supply its own
    // recurrence engine; defaults to recurrence.ts.
    readonly findNextOccurrence?: FindNextOccurrence;
    readonly idleMs?: number;
    readonly registerKinds?: boolean;
    readonly exit: () => void;
    readonly log: (message: string) => void;
}

export interface RunningService {
    readonly bus: Bus;
    readonly close: () => void;
}

const nowZeroed = (): number => Math.floor(Date.now() / 1000) * 1000;

export const createReminderService = (deps: ReminderServiceDeps) => async (): Promise<RunningService> => {
    const activity = deps.createActivity();
    // Private bus only: every caller (activity manager, the calendar app, boot)
    // is on the private side, as HP's services.json had no public commands.
    const bus = deps.openBus(SERVICE_NAME, { activity });
    const db = createDb8(bus);
    const now = deps.now ?? nowZeroed;

    if (deps.registerKinds !== false) {
        for (const kind of KINDS) {
            await db.putKind(kind).catch((error: unknown) =>
                deps.log(`could not register kind ${kind.id}: ${String(error)}`));
        }
    }

    const reminders = createReminders({ db, bus, log: deps.log, now });

    const allEvents = async (): Promise<(CalendarEvent & DbObject)[]> => {
        const { results } = await db.find<CalendarEvent & DbObject>({ from: EVENT_KIND, orderBy: "_rev" });
        return results;
    };

    const commands = createCommands({
        bus, reminders, allEvents,
        findNextOccurrence: deps.findNextOccurrence ?? defaultFindNextOccurrence,
        now, log: deps.log,
    });

    // Registered on the one private bus, with mojoservice's timeout, plus
    // __quit -- the shape serveOnDemand uses, inlined here because the kinds and
    // command wiring above need to happen first and asynchronously.
    for (const command of commands) {
        bus.method(command.name, command.handler, { timeout: command.timeout ?? DEFAULT_COMMAND_TIMEOUT });
    }
    const quit: Handler = () => {
        setTimeout(deps.exit, QUIT_DELAY_MS);
        return {};
    };
    bus.method("__quit", quit);

    activity.exitWhenIdle(deps.idleMs ?? DEFAULT_IDLE_MS, deps.exit);

    return {
        bus,
        close: () => {
            bus.close();
            activity.stop();
        },
    };
};
