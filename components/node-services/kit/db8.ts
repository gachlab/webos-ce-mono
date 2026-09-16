// db8, the webOS database, for services written in modern TypeScript.
//
//   const db = createDb8(bus);
//   const { results } = await db.find({ from: "com.example.item:1", where: [...] });
//   for await (const item of db.findAll({ from: "com.example.item:1" })) { ... }
//   for await (const results of db.watchFind(query, { signal })) { ... }
//
// Only a thin layer over com.palm.db's methods: the payloads are db8's own,
// documented in components/db8, and the replies come back as they are, without
// returnValue. What it adds is paging as iteration and watches as iteration.

import type { Bus, Payload } from "./luna.ts";

export interface DbObject extends Payload {
    _id?: string;
    _rev?: number;
    _kind?: string;
    _del?: boolean;
}

export interface Query extends Payload {
    from: string;
    where?: readonly Payload[];
    filter?: readonly Payload[];
    select?: readonly string[];
    orderBy?: string;
    desc?: boolean;
    incDel?: boolean;
    limit?: number;
    page?: string;
}

export interface FindReply<T extends DbObject> {
    readonly results: T[];
    // Present when there are more results: pass it back as query.page.
    readonly next?: string;
    // Present when the find asked for a count.
    readonly count?: number;
}

export interface PutResult {
    readonly id: string;
    readonly rev: number;
}

export interface WatchOptions {
    // Aborting it cancels the watch and ends the iteration.
    readonly signal?: AbortSignal;
}

export interface Db8 {
    find<T extends DbObject = DbObject>(query: Query, options?: { count?: boolean }): Promise<FindReply<T>>;
    // Every result of the query, fetching page after page as the loop asks.
    findAll<T extends DbObject = DbObject>(query: Query): AsyncGenerator<T, void, undefined>;
    get<T extends DbObject = DbObject>(ids: readonly string[]): Promise<T[]>;
    put(objects: readonly DbObject[]): Promise<PutResult[]>;
    merge(objects: readonly DbObject[]): Promise<PutResult[]>;
    mergeWhere(query: Query, props: Payload): Promise<number>;
    del(ids: readonly string[], options?: { purge?: boolean }): Promise<PutResult[]>;
    delWhere(query: Query, options?: { purge?: boolean }): Promise<number>;
    // The query's results now, and again every time something changes them,
    // until the loop ends or the signal is aborted. db8's watches fire once;
    // this re-arms them.
    watchFind<T extends DbObject = DbObject>(query: Query, options?: WatchOptions): AsyncGenerator<T[], void, undefined>;
    batch(operations: readonly { method: string; params: Payload }[]): Promise<Payload[]>;
    putKind(kind: Payload): Promise<void>;
    delKind(id: string): Promise<void>;
}

type Caller = Pick<Bus, "call" | "subscribe">;

export const createDb8 = (bus: Caller, service = "com.palm.db"): Db8 => {
    const uri = (method: string) => `luna://${service}/${method}`;
    const call = <R extends Payload>(method: string, payload: Payload) => bus.call<R>(uri(method), payload);

    // The reply as FindReply: results, and next and count only when db8 sent them.
    const find: Db8["find"] = async <T extends DbObject>(query: Query, options: { count?: boolean } = {}) => {
        const reply = await call<{ results: T[]; next?: string; count?: number }>("find",
            options.count ? { query, count: true } : { query });
        return {
            results: reply.results,
            ...(reply.next === undefined ? {} : { next: reply.next }),
            ...(reply.count === undefined ? {} : { count: reply.count }),
        };
    };

    const findAll: Db8["findAll"] = async function* <T extends DbObject>(query: Query) {
        for (let page: string | undefined = query.page; ;) {
            const reply: FindReply<T> = await find<T>(page === undefined ? query : { ...query, page });
            yield* reply.results;
            if (reply.next === undefined) {
                return;
            }
            page = reply.next;
        }
    };

    const watchFind: Db8["watchFind"] = async function* <T extends DbObject>(query: Query, options: WatchOptions = {}) {
        const { signal } = options;
        while (!signal?.aborted) {
            // find with watch: the first reply carries the results. The only
            // reply db8 sends after it is {fired: true}, when they changed.
            const replies = bus.subscribe<FindReply<T> & Payload>(uri("find"), { query, watch: true },
                signal ? { signal } : {});
            try {
                const first = await replies.next();
                if (first.done) {
                    return;
                }
                yield first.value.results;
                if ((await replies.next()).done) {
                    return;
                }
            } finally {
                await replies.return?.();
            }
        }
    };

    return {
        find,
        findAll,
        watchFind,
        get: async <T extends DbObject>(ids: readonly string[]) =>
            (await call<{ results: T[] }>("get", { ids })).results,
        put: async (objects) => (await call<{ results: PutResult[] }>("put", { objects })).results,
        merge: async (objects) => (await call<{ results: PutResult[] }>("merge", { objects })).results,
        mergeWhere: async (query, props) => (await call<{ count: number }>("merge", { query, props })).count,
        del: async (ids, options = {}) =>
            (await call<{ results: PutResult[] }>("del", { ids, ...(options.purge ? { purge: true } : {}) })).results,
        delWhere: async (query, options = {}) =>
            (await call<{ count: number }>("del", { query, ...(options.purge ? { purge: true } : {}) })).count,
        batch: async (operations) => (await call<{ responses: Payload[] }>("batch", { operations })).responses,
        putKind: async (kind) => void await call("putKind", kind),
        delKind: async (id) => void await call("delKind", { id }),
    };
};
