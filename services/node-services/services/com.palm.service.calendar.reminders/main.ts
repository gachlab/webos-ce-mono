// Entry point: `node main.ts`, started by the hub on demand.

import { createActivity, openBus } from "#kit/luna.ts";
import { createReminderService } from "./service.ts";

await createReminderService({
    openBus,
    createActivity: () => createActivity({
        setTimer: (callback, ms) => setTimeout(callback, ms),
        clearTimer: (timer) => clearTimeout(timer as ReturnType<typeof setTimeout> | undefined),
    }),
    exit: () => process.exit(0),
    log: (message) => console.log(`com.palm.service.calendar.reminders: ${message}`),
})();
