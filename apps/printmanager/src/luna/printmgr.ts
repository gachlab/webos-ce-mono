// com.palm.printmgr, typed.
//
// Vocabulary matches what enyo's PrintDialog expects of the service and what
// services/printmgr-cups answers (see its print_state.h): the printers/list
// Add/Rmv events with printerID/printerName/printerAddress, printers/getCurrent,
// and the jobs/* lifecycle. The Print Manager card is a reader of printers and a
// canceller of jobs; the dialog drives the open/addFile/close flow, so the card
// only needs the subset below.

import type { LunaService, Payload, Subscription } from "@webos/api/infra/luna/service.ts";

export interface Printer {
    readonly id: string;
    readonly name: string;
    readonly address: string;
}

const text = (value: unknown): string => (typeof value === "string" ? value : "");

const printerOf = (raw: Payload): Printer => ({
    id: text(raw.printerID),
    name: text(raw.printerName) || text(raw.printerID),
    address: text(raw.printerAddress),
});

export interface PrintmgrClient {
    // printers/list is a subscription of Add/Rmv events; the card folds them
    // into a set keyed by printerID.
    watchPrinters(onAdd: (printer: Printer) => void,
                  onRemove: (printerId: string) => void): Subscription;
    getCurrent(): Promise<Printer | undefined>;
    setCurrent(printerId: string): Promise<void>;
    cancelJob(jobID: number): Promise<void>;
}

export const createPrintmgr = (luna: LunaService): PrintmgrClient => {
    const printers = "luna://com.palm.printmgr/printers/";
    const jobs = "luna://com.palm.printmgr/jobs/";

    return {
        watchPrinters(onAdd, onRemove) {
            return luna.subscribe(`${printers}list`, { subscribe: true }, (payload) => {
                const id = text(payload.printerID);
                if (!id)
                    return;
                const event = text(payload.eventType);
                if (event === "Rmv")
                    onRemove(id);
                else
                    onAdd(printerOf(payload));
            });
        },

        async getCurrent() {
            const reply = await luna.call(`${printers}getCurrent`, {});
            const id = text((reply as Payload).printerID);
            return id ? printerOf(reply as Payload) : undefined;
        },

        async setCurrent(printerId) {
            await luna.call(`${printers}setCurrent`, { printerID: printerId });
        },

        async cancelJob(jobID) {
            await luna.call(`${jobs}cancel`, { jobID });
        },
    };
};
