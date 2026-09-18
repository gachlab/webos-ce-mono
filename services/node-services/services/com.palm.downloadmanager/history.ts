// The downloads com.palm.downloadmanager remembers, and the next ticket.
//
// HP kept them in /var/luna/data/downloadhistory.db (sqlite). Callers only see
// getAllHistory's items -- {state, fileExistsOnFilesys, recordString} with the
// final record as JSON -- so a JSON file does the same job.

import type { Payload } from "#kit/luna.ts";

export type HistoryState = "completed" | "cancelled" | "interrupted" | "failed";

export interface HistoryEntry {
    readonly ticket: number;
    // Who asked for the download: getAllHistory and clearHistory go by it.
    readonly owner: string;
    readonly state: HistoryState;
    // The download's final record, as its subscribers last saw it.
    readonly record: Payload;
}

interface Stored {
    nextTicket: number;
    entries: HistoryEntry[];
}

export interface HistoryFiles {
    read(): string | undefined;
    write(text: string): void;
}

export interface History {
    nextTicket(): number;
    add(entry: HistoryEntry): void;
    get(ticket: number): HistoryEntry | undefined;
    // Oldest ticket first, as the browser expects.
    of(owner: string): HistoryEntry[];
    remove(ticket: number): void;
    clear(owner: string): void;
}

// How many downloads are remembered; the oldest are forgotten first.
export const HISTORY_LIMIT = 500;

const load = (files: HistoryFiles): Stored => {
    try {
        const text = files.read();
        const stored = text ? (JSON.parse(text) as Partial<Stored>) : {};
        const entries = Array.isArray(stored.entries) ? stored.entries : [];
        const highest = entries.reduce((max, entry) => Math.max(max, entry.ticket), 0);
        return { nextTicket: Math.max(Number(stored.nextTicket) || 1, highest + 1), entries };
    } catch {
        return { nextTicket: 1, entries: [] };
    }
};

export const createHistory = (files: HistoryFiles, limit = HISTORY_LIMIT): History => {
    const stored = load(files);
    const save = () => files.write(JSON.stringify(stored));
    return {
        nextTicket: () => {
            const ticket = stored.nextTicket++;
            save();
            return ticket;
        },
        add: (entry) => {
            stored.entries = [...stored.entries.filter((e) => e.ticket !== entry.ticket), entry]
                .sort((a, b) => a.ticket - b.ticket)
                .slice(-limit);
            save();
        },
        get: (ticket) => stored.entries.find((entry) => entry.ticket === ticket),
        of: (owner) => stored.entries.filter((entry) => entry.owner === owner),
        remove: (ticket) => {
            stored.entries = stored.entries.filter((entry) => entry.ticket !== ticket);
            save();
        },
        clear: (owner) => {
            stored.entries = stored.entries.filter((entry) => entry.owner !== owner);
            save();
        },
    };
};
