// The Print Manager card: the printers CUPS knows, with the current one marked
// and selectable. Screens and wording follow HP's Print Manager app (spec, not
// code). The service is com.palm.printmgr from services/printmgr-cups on CUPS.
//
// HP's Print Manager also showed the active print jobs so they could be
// cancelled. Our service's jobs are opened by the print dialog and tracked in
// its own session; the card lists printers and sets the default here, and
// cancelling a job is reached from the dialog's own progress popup. If a jobs
// query is added to the service later, the job list folds in through the same
// state.

import { createState, type State, type StateHolder, type Unsubscribe } from "@webos/api/helpers/create-state.ts";
import { createPrintmgr, type Printer } from "./luna/printmgr.ts";
import { LunaCallError, errorTextOf, type LunaService, type Subscription } from "@webos/api/infra/luna/service.ts";
import type { LaunchParams } from "@webos/api/infra/app/service.ts";

export interface PrintData {
    readonly printers: Printer[];
    readonly currentId: string;
    readonly searching: boolean;
    readonly message: string;
}

export interface PrintService {
    getState(): State<PrintData>;
    onStateChange(listener: (state: State<PrintData>) => void): Unsubscribe;
    onShown(params?: LaunchParams): void;
    onHidden(): void;
    onBack(): boolean;
    dispose(): void;

    onSelectPrinter(printerId: string): void;
}

export const createPrintService = (luna: LunaService): PrintService => {
    const pm = createPrintmgr(luna);
    const state: StateHolder<PrintData> = createState<PrintData>({
        name: "print:list",
        data: {
            printers: [],
            currentId: "",
            searching: true,
            message: "",
        },
    });
    let gone = false;
    const subscriptions: Subscription[] = [];

    const fail = (error: unknown) => {
        if (gone)
            return;
        const message = error instanceof LunaCallError ? errorTextOf(error.reply)
            : error instanceof Error ? error.message : String(error);
        state.patch({ searching: false, message });
    };

    const addPrinter = (printer: Printer) => {
        if (gone)
            return;
        const printers = state.get().data.printers.slice();
        const at = printers.findIndex((p) => p.id === printer.id);
        if (at >= 0)
            printers[at] = printer;      // update in place
        else
            printers.push(printer);
        state.patch({ printers, searching: false });
    };

    const removePrinter = (printerId: string) => {
        if (gone)
            return;
        const printers = state.get().data.printers.filter((p) => p.id !== printerId);
        state.patch({ printers });
    };

    return {
        getState: () => state.get(),
        onStateChange: (listener) => state.subscribe(listener),

        onShown() {
            gone = false;
            if (subscriptions.length > 0)
                return;
            state.patch({ searching: true, message: "" });
            subscriptions.push(pm.watchPrinters(addPrinter, removePrinter));
            void pm.getCurrent().then((printer) => {
                if (!gone && printer)
                    state.patch({ currentId: printer.id });
            }).catch(fail);
        },

        onHidden() {
            gone = true;
            while (subscriptions.length > 0)
                subscriptions.pop()?.cancel();
        },

        onBack() {
            return false; // one screen; let the shell close the card
        },

        dispose() {
            this.onHidden();
        },

        onSelectPrinter(printerId) {
            const current = state.get().data.currentId;
            if (current === printerId)
                return;
            state.patch({ currentId: printerId, message: "" });
            void pm.setCurrent(printerId).catch((error) => {
                // Roll the selection back if the service refused it.
                if (!gone)
                    state.patch({ currentId: current });
                fail(error);
            });
        },
    };
};
