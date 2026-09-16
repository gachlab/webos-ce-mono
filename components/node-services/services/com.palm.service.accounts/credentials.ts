// Credentials, stored in db8 (com.palm.account.credentials:1) as HP's
// models/credentials-model.js stored them: one object per account and name,
// keyed "<accountId>:<name>".

import type { Db8 } from "#kit/db8.ts";
import type { Payload } from "#kit/luna.ts";

export const CREDENTIALS_KIND = "com.palm.account.credentials:1";

export interface KeyStore {
    put(accountId: string, name: string, value: unknown): Promise<void>;
    // Throws when there is none, as HP's did.
    get(accountId: string, name: string): Promise<{ credentials: unknown }>;
    // Removes every credential of the account.
    del(accountId: string): Promise<void>;
    has(accountId: string): Promise<{ value: boolean }>;
}

const keyOf = (accountId: string, name: string) => `${accountId}:${name}`;

export const createKeyStore = (db: Db8): KeyStore => {
    const find = (accountId: string, name: string) =>
        db.find({ from: CREDENTIALS_KIND, where: [{ prop: "key", op: "=", val: keyOf(accountId, name) }] });

    return {
        put: async (accountId, name, value) => {
            const { results } = await find(accountId, name);
            const existing = results[0];
            await db.put([{
                _kind: CREDENTIALS_KIND,
                ...(existing ? { _id: existing._id, _rev: existing._rev } : {}),
                key: keyOf(accountId, name),
                val: value,
            } as Payload]);
        },
        get: async (accountId, name) => {
            const { results } = await find(accountId, name);
            const found = results[0];
            if (!found) {
                throw new Error(`Credentials not found: ${name}`);
            }
            return { credentials: found.val };
        },
        del: async (accountId) => {
            await db.delWhere({ from: CREDENTIALS_KIND, where: [{ prop: "key", op: "%", val: keyOf(accountId, "") }] });
        },
        has: async (accountId) => {
            const { results } = await db.find({ from: CREDENTIALS_KIND, where: [{ prop: "key", op: "%", val: accountId }] });
            return { value: results.length > 0 };
        },
    };
};

// Saves each named credential, all at once.
export const saveCredentials = async (keys: KeyStore, accountId: string,
                                      credentials: Record<string, unknown> | undefined): Promise<void> => {
    if (credentials) {
        await Promise.all(Object.entries(credentials).map(([name, value]) => keys.put(accountId, name, value)));
    }
};
