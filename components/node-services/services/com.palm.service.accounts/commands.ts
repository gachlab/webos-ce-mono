// The commands of com.palm.service.accounts, rewritten from HP's handlers/*.js.
//
// Each keeps HP's method name, parameters, replies and side effects, with two
// deliberate differences, noted where they happen:
// * a transport callback that fails stops the sequence and fails the command,
//   where HP's left the command waiting until its hour-long timeout;
// * each onEnabled call carries its own capabilityProviderId, where HP's shared
//   one parameter object and sent them all the last one.

import type { Db8 } from "#kit/db8.ts";
import type { Bus, Payload, Request } from "#kit/luna.ts";
import { mojoError, mojoHandler, type Command } from "#kit/mojoservice.ts";
import {
    ACCOUNT_KIND, PROFILE_TEMPLATE, annotate, diffLists, findTemplate, providerStates, publicAccessByCapability,
    publicAccessById, requirePermission, storedProviders,
    type Account, type CapabilityProvider, type Template,
} from "./accounts.ts";
import { saveCredentials, type KeyStore } from "./credentials.ts";

export const SELF = "luna://com.palm.service.accounts";
const SYNCSTATE_KIND = "com.palm.account.syncstate:1";

export interface Delays {
    // Before notifying transports of a new account, so the UI shows it first.
    readonly beforeNotifyCreated: number;
    // After marking an account as being deleted, so account lists catch up.
    readonly afterMarkDeleted: number;
    // Before notifying transports of a deletion.
    readonly beforeNotifyDeleted: number;
}

export const HP_DELAYS: Delays = { beforeNotifyCreated: 1000, afterMarkDeleted: 1500, beforeNotifyDeleted: 2000 };

export interface CommandDeps {
    readonly bus: Pick<Bus, "call">;
    readonly db: Db8;
    readonly tempdb: Db8;
    readonly keys: KeyStore;
    readonly templates: () => readonly Template[];
    readonly reloadTemplates: () => Promise<void>;
    // Resolves once the templates are first loaded; commands wait for it.
    readonly ready: Promise<void>;
    readonly sleep: (ms: number) => Promise<void>;
    readonly delays: Delays;
    // Leaves the flag first use checks for a profile account made without it.
    readonly markProfileCreated: () => void;
    readonly log: (message: string) => void;
}

type Args = Payload;

// Foundations' Assert.require: a plain Error, so mojoservice's -9999 reply.
const require = (condition: unknown, message = "Assert.require failed"): void => {
    if (!condition) {
        throw new Error(message);
    }
};

const where = (prop: string, val: unknown, op = "=") => ({ prop, op, val });

const asAccount = (value: unknown): Account => value as Account;

export const createCommands = (deps: CommandDeps): Command[] => {
    const { bus, db, tempdb, keys, log } = deps;

    // A call nobody waits for; its failure is only logged.
    const fire = (uri: string, params: Payload) =>
        void bus.call(uri, params).catch((error: unknown) => log(`call to ${uri} failed: ${String(error)}`));

    // Every call at once; the failed ones are returned, not thrown.
    const callAll = async (calls: readonly { uri: string; params: Payload; providerId?: string }[]) => {
        const settled = await Promise.allSettled(calls.map((c) => bus.call(c.uri, c.params)));
        return calls.map((c, i) => ({ ...c, outcome: settled[i]! }))
            .filter((c) => c.outcome.status === "rejected");
    };

    // HP's transports signalled success with returnValue; a failure stops.
    const callEach = async (calls: readonly { uri: string; params: Payload }[], what: string) => {
        const failed = await callAll(calls);
        if (failed.length > 0) {
            throw mojoError("TRANSPORT_FAILURE", `${what} failed for ${failed.map((f) => f.uri).join(",")}`);
        }
    };

    const getAccount = async (accountId: unknown): Promise<Account> => {
        const [found] = await db.get([String(accountId)]);
        require(found);
        return asAccount(found);
    };

    const withTemplates = <A extends Args>(run: (args: A, request: Request<A>) => Promise<Payload | void> | Payload | void) =>
        mojoHandler<A>(async (request) => {
            await deps.ready;
            return run(request.payload, request);
        });

    const listAccounts = async (args: Args, applicationId: string | undefined) => {
        const conditions = args.templateId
            ? [where("templateId", args.templateId)]
            : args.capability
                ? [where("capabilityProviders.capability", args.capability)]
                : [];
        log(`ListAccounts: ${JSON.stringify(conditions)} by ${applicationId ?? ""}`);
        const { results } = await db.find({ from: ACCOUNT_KIND, where: [...conditions, where("beingDeleted", false)] });
        const annotated: Account[] = [];
        for (const account of results) {
            try {
                annotated.push(annotate(asAccount(account), deps.templates()));
            } catch {
                log(`ListAccounts: skipping account because template ${String(account.templateId)} is missing`);
            }
        }
        return { results: annotated };
    };

    const denied = () => mojoError("", "Permission denied.");

    // ---- createAccount -------------------------------------------------------

    // Every validator is asked; what they return is merged. A validator that
    // fails is only logged, as HP's mapReduce did.
    const checkPassword = async (account: Account, password: string, validators: readonly unknown[], config: unknown) => {
        const replies = await Promise.allSettled(validators.map(async (validator) => {
            if (typeof validator !== "string" || validator.lastIndexOf("/") === -1) {
                throw new Error(`invalid validator: ${String(validator)}`);
            }
            return bus.call(validator, {
                username: account.username, password, templateId: account.templateId, config,
            });
        }));
        const merged: Payload = {};
        const credentials: Payload = {};
        for (const reply of replies) {
            if (reply.status === "fulfilled") {
                Object.assign(merged, reply.value);
                Object.assign(credentials, reply.value.credentials as Payload | undefined);
            } else {
                log(`validator returned error: ${String(reply.reason)}`);
            }
        }
        return { ...merged, credentials } as Payload & { credentials: Payload };
    };

    const createAccount = withTemplates(async (args, request) => {
        require(args.templateId, "missing templateId");
        require(Array.isArray(args.capabilityProviders), "missing capabilityProviders");
        require(args.username, "missing username");
        require(!(args.password && args.credentials), "can't have both password and credentials");

        const template = findTemplate(deps.templates(), String(args.templateId));
        if (!template) {
            throw new Error(`no account template for id=${String(args.templateId)}`);
        }
        if (template.validator) {
            require(typeof args.password === "string" || typeof args.credentials === "object");
        }
        requirePermission("writePermissions", template, request);

        const selected: CapabilityProvider[] = [];
        for (const wanted of args.capabilityProviders as { id: string }[]) {
            if (selected.some((provider) => provider.id === wanted.id)) {
                continue;
            }
            const provider = template.capabilityProviders.find((candidate) => candidate.id === wanted.id);
            if (provider) {
                require(provider.capability, "capability missing!");
                selected.push(provider);
            } else {
                log(`Requested capability provider: ${wanted.id} not found, ignoring`);
            }
        }

        const { results: duplicates } = await db.find({
            from: ACCOUNT_KIND,
            where: [where("beingDeleted", false), where("templateId", template.templateId), where("username", args.username)],
        });
        if (duplicates.length > 0) {
            throw mojoError("DUPLICATE_ACCOUNT", "Unable to create a duplicate account");
        }

        const account: Account = {
            _kind: ACCOUNT_KIND,
            templateId: template.templateId,
            username: String(args.username),
            ...(args.alias !== undefined ? { alias: String(args.alias) } : {}),
            beingDeleted: false,
            capabilityProviders: selected.map((provider) => ({ id: provider.id, capability: provider.capability! })),
            ...(args._sync !== undefined ? { _sync: args._sync } : {}),
        };

        let credentials = args.credentials as Payload | undefined;
        let config = args.config;
        if (args.password) {
            const validators = [template.validator, ...selected.flatMap((provider) => provider.validator ? [provider.validator] : [])];
            const checked = await checkPassword(account, String(args.password), validators, config);
            credentials = checked.credentials;
            if (checked.config) {
                config = checked.config;
            }
        }

        const [written] = await db.put([account]);
        account._id = written!.id;
        await saveCredentials(keys, account._id, credentials);

        fire(`${SELF}/notifyAccountCreated`, { accountId: account._id, config });
        return { result: account };
    });

    // ---- modifyAccount -------------------------------------------------------

    const modifyAccount = withTemplates(async (args, request) => {
        const accountId = String(args.accountId);
        const changes = (args.object ?? {}) as Partial<Account> & { credentials?: Payload };
        log(`ModifyCommandAssistant: id=${accountId}`);

        if (changes.credentials) {
            // Clears the account's status, so credential error dashboards go.
            await tempdb.delWhere({ from: SYNCSTATE_KIND, where: [where("accountId", accountId)] });
        }

        const account = await getAccount(accountId);
        // The template may be gone (its app was removed). The accounts app may
        // still change the account; what needs the template is skipped, as in
        // HP's code.
        const template = findTemplate(deps.templates(), account.templateId);
        requirePermission("writePermissions", template, request);
        const templateProviders = template?.capabilityProviders ?? [];

        const hadCredentials = (await keys.has(account._id!)).value;
        if (hadCredentials && changes.credentials) {
            await keys.del(account._id!);
        }
        await saveCredentials(keys, account._id!, changes.credentials);

        const newProviders = changes.capabilityProviders;
        const diff = newProviders
            ? diffLists(account.capabilityProviders.map((p) => p.id), newProviders.map((p) => p.id))
            : { add: [], remove: [] };

        const disablesAlwaysOn = diff.remove.some((id) =>
            templateProviders.find((provider) => provider.id === id)?.alwaysOn);
        if (disablesAlwaysOn) {
            throw mojoError("400_BAD_REQUEST", "can't disable 'alwaysOn' capabilities");
        }

        await db.merge([{
            _id: accountId,
            username: changes.username,
            alias: changes.alias,
            capabilityProviders: template && Array.isArray(newProviders) ? storedProviders(template, newProviders) : undefined,
        }]);

        const changed = diff.add.length + diff.remove.length > 0;
        if (changed && template?.onCapabilitiesChanged) {
            await bus.call(template.onCapabilitiesChanged, {
                accountId, capabilityProviders: providerStates(template, newProviders ?? []),
            });
        }

        const failed: { uri: string; providerId?: string }[] = [];
        if (changes.credentials && !args.suppressNotifications) {
            // Every interested transport hears of new credentials. (HP meant to
            // skip the providers being enabled, but its check never matched,
            // so all of them were called; that is kept.)
            const params = { accountId };
            const calls = [
                ...(template?.onCredentialsChanged ? [{ uri: template.onCredentialsChanged, params }] : []),
                ...(newProviders ?? account.capabilityProviders).flatMap((wanted) => {
                    const provider = templateProviders.find((candidate) => candidate.id === wanted.id);
                    return provider?.onCredentialsChanged
                        ? [{ uri: provider.onCredentialsChanged, params, providerId: provider.id }] : [];
                }),
            ];
            failed.push(...await callAll(calls));
        }

        if (failed.length === 0 && changed) {
            const onEnabled = (enabled: boolean) => (id: string) => {
                const provider = templateProviders.find((candidate) => candidate.id === id);
                return provider?.onEnabled
                    ? [{ uri: provider.onEnabled, params: { accountId, enabled, capabilityProviderId: id }, providerId: id }]
                    : [];
            };
            failed.push(...await callAll([...diff.add.flatMap(onEnabled(true)), ...diff.remove.flatMap(onEnabled(false))]));
        }

        if (failed.length > 0 && template && newProviders) {
            // Undo the change for the providers whose transports failed. When
            // the request changed no providers there is nothing to undo.
            const ids = newProviders.map((provider) => provider.id);
            for (const { providerId } of failed) {
                if (providerId === undefined) {
                    continue;
                }
                const at = ids.indexOf(providerId);
                if (at === -1) {
                    ids.push(providerId);
                } else {
                    ids.splice(at, 1);
                }
            }
            await db.merge([{ _id: accountId, capabilityProviders: storedProviders(template, ids.map((id) => ({ id }))) }]);
        }
        if (failed.length > 0) {
            throw mojoError("TRANSPORT_FAILURE", `Failed to notify transports: ${failed.map((f) => f.uri).join(",")}`);
        }
        return {};
    });

    // ---- deleteAccount -------------------------------------------------------

    const deleteAccount = withTemplates(async (args, request) => {
        const accountId = String(args.accountId);
        log(`DeleteCommandAssistant: deleting account ${accountId}`);
        const [found] = await db.get([accountId]);
        if (!found) {
            throw new Error("Unable to find account");
        }
        const account = asAccount(found);
        if (account._del) {
            throw new Error("Account has been deleted");
        }
        if (account.beingDeleted) {
            throw new Error("Account is being deleted");
        }
        requirePermission("writePermissions", annotate(account, deps.templates()), request);

        await db.merge([{ _id: accountId, beingDeleted: true }]);
        await deps.sleep(deps.delays.afterMarkDeleted);
        // Shows "Removing Account" even for transports that do not say so.
        await tempdb.put([{ _kind: SYNCSTATE_KIND, accountId, syncState: "DELETE", capabilityProvider: "com.palm.service.accounts" }]);
        fire(`${SELF}/notifyAccountDeleted`, { accountId });
        return {};
    });

    // ---- credentials ---------------------------------------------------------

    const readCredentials = withTemplates(async (args, request) => {
        const account = await getAccount(args.accountId);
        requirePermission("readPermissions", findTemplate(deps.templates(), account.templateId), request);
        return keys.get(String(args.accountId), String(args.name));
    });

    const writeCredentials = withTemplates(async (args, request) => {
        const accountId = String(args.accountId);
        const annotated = annotate(await getAccount(accountId), deps.templates());
        requirePermission("writePermissions", annotated, request);
        await keys.put(accountId, String(args.name), args.credentials);
        const params = { accountId };
        if (annotated.onCredentialsChanged) {
            fire(String(annotated.onCredentialsChanged), params);
        }
        for (const provider of annotated.capabilityProviders) {
            if (provider.onCredentialsChanged && provider._id) {
                fire(provider.onCredentialsChanged, params);
            }
        }
        return {};
    });

    // ---- notifications to transports ----------------------------------------

    const notifyAccountCreated = withTemplates(async (args) => {
        const accountId = String(args.accountId);
        log(`notifyAccountCreated: letting transports know of new account ${accountId}`);
        const account = await getAccount(accountId);
        await deps.sleep(deps.delays.beforeNotifyCreated);
        const annotated = annotate(account, deps.templates());

        await callEach(annotated.capabilityProviders.flatMap((provider) => provider.onCreate
            ? [{ uri: provider.onCreate, params: { accountId, config: args.config } }] : []), "onCreate");

        await callEach(annotated.capabilityProviders.flatMap((provider) => provider.onEnabled && provider._id
            ? [{ uri: provider.onEnabled, params: { accountId, capabilityProviderId: provider.id, enabled: true } }] : []),
        "onEnabled");

        if (annotated.onCapabilitiesChanged) {
            const template = findTemplate(deps.templates(), annotated.templateId)!;
            await bus.call(String(annotated.onCapabilitiesChanged), {
                accountId: annotated._id, capabilityProviders: providerStates(template, annotated.capabilityProviders),
            });
        }
        log(`notifyAccountCreated: transports have been notified ${accountId}`);
        return {};
    });

    const notifyAccountDeleted = withTemplates(async (args) => {
        const accountId = String(args.accountId);
        log(`notifyAccountDeleted: deleting account ${accountId}`);
        const account = await getAccount(accountId);
        await deps.sleep(deps.delays.beforeNotifyDeleted);
        const annotated = annotate(account, deps.templates());

        // A failure stops here, and the account stays marked; stayRunning
        // retries it later.
        await callEach(annotated.capabilityProviders.flatMap((provider) => provider.onEnabled && provider._id
            ? [{ uri: provider.onEnabled, params: { accountId, capabilityProviderId: provider.id, enabled: false } }] : []),
        "onEnabled");

        const template = findTemplate(deps.templates(), account.templateId);
        await callEach((template?.capabilityProviders ?? []).flatMap((provider) => provider.onDelete
            ? [{ uri: provider.onDelete, params: { accountId } }] : []), "onDelete");

        if ((await keys.has(accountId)).value) {
            await keys.del(accountId);
        }
        await tempdb.delWhere({ from: SYNCSTATE_KIND, where: [where("accountId", accountId)] });
        await db.del([accountId]);
        log(`notifyAccountDeleted: deleted account ${accountId}`);
        return {};
    });

    // ---- restore and upkeep --------------------------------------------------

    const profileDeviceName = (deviceName: unknown) =>
        void db.mergeWhere({
            from: ACCOUNT_KIND,
            where: [where("templateId", PROFILE_TEMPLATE), where("beingDeleted", false)],
        }, { deviceName }).catch((error: unknown) => log(`saving the device name failed: ${String(error)}`));

    // Called by first use once accounts are restored, and by the backup
    // service when a restore finishes: every transport is told to start again.
    const restoreCompleted = withTemplates(async () => {
        log("RestoreCompletedCommandAssistant");
        const { results } = await db.find({ from: ACCOUNT_KIND });
        const accounts = results.map(asAccount);
        const calls: { uri: string; params: Payload }[] = [];

        for (const stored of [...accounts].reverse()) {
            if (stored.beingDeleted) {
                continue;
            }
            const template = findTemplate(deps.templates(), stored.templateId);
            if (!template) {
                continue;
            }
            const account = annotate(stored, [template]);
            if (account.onCapabilitiesChanged) {
                calls.push({
                    uri: String(account.onCapabilitiesChanged),
                    params: { accountId: account._id, capabilityProviders: providerStates(template, account.capabilityProviders) },
                });
            }
            for (const provider of [...account.capabilityProviders].reverse()) {
                if (provider.onEnabled) {
                    calls.push({
                        uri: provider.onEnabled,
                        params: { accountId: account._id, capabilityProviderId: provider.id, enabled: true },
                    });
                }
            }
            // The profile account always has every capability enabled.
            if (account.templateId === PROFILE_TEMPLATE) {
                void db.merge([{
                    _id: account._id!,
                    capabilityProviders: template.capabilityProviders.map((p) => ({ id: p.id, capability: p.capability })),
                }]).catch((error: unknown) => log(`enabling the profile's capabilities failed: ${String(error)}`));
            }
        }

        // Waited for, so the transports set up their syncs before anything else.
        for (const failure of await callAll(calls)) {
            log(`WARNING: transport call failed: ${failure.uri}`);
        }

        for (const account of accounts) {
            const unusedTelephony = account.templateId === "com.palm.telephony" && Boolean(account._sync);
            if (unusedTelephony || account.beingDeleted) {
                log(`Deleting ${account.templateId} account ${String(account._id)}`);
                void db.del([account._id!]).catch((error: unknown) => log(`deleting failed: ${String(error)}`));
            }
        }

        const prefs = await bus.call("luna://com.palm.systemservice/getPreferences", { keys: ["deviceName"] });
        profileDeviceName(prefs.deviceName ?? "");
        return {};
    });

    const resumeAccountDeletes = async () => {
        const { results } = await db.find({ from: ACCOUNT_KIND, where: [where("beingDeleted", true)] });
        const pending = results.map(asAccount).filter((account) => !account.retries || account.retries < 5);
        if (pending.length === 0) {
            log("resumeAccountDeletes: No accounts to delete");
            return;
        }
        const retried = pending.map((account) => ({ _id: account._id!, retries: (account.retries ?? 0) + 1 }));
        await db.merge(retried);
        await Promise.allSettled(retried.map((account) => {
            log(`resumeAccountDeletes: resuming delete of account ${account._id}, retry #${account.retries}`);
            return bus.call(`${SELF}/notifyAccountDeleted`, { accountId: account._id });
        }));
    };

    // Keeps the service up (it is asked to on startup), then finishes any
    // deletion that was interrupted.
    const stayRunning = mojoHandler(async ({ payload }) => {
        const asked = Number(payload.seconds);
        const seconds = payload.seconds ? Math.min(asked, 3600) : 30;
        log(`Accounts service running for ${seconds} seconds ...`);
        await deps.sleep(seconds * 1000);
        void resumeAccountDeletes().catch((error: unknown) => log(`resumeAccountDeletes failed: ${String(error)}`));
        return {};
    });

    // Makes the profile account that first use would have made, when it was skipped.
    const createLocalAccount = withTemplates(async () => {
        const { results } = await bus.call<{ results: unknown[] }>(`${SELF}/listAccounts`, { templateId: PROFILE_TEMPLATE });
        if (results.length > 0) {
            return { accountCreated: false };
        }
        const username = "Open webOS";
        log(`Creating a palmprofile account with username: ${username}`);
        void bus.call(`${SELF}/createAccount`, {
            templateId: PROFILE_TEMPLATE,
            capabilityProviders: ["contacts", "calendar", "tasks", "memos", "sms", "voice", "localfilestore"]
                .map((name) => ({ id: `${PROFILE_TEMPLATE}.${name}` })),
            username,
            credentials: {},
        }).then(() => deps.markProfileCreated())
            .catch((error: unknown) => log(`local account creation, or its flag, failed: ${String(error)}`));
        return { accountCreated: true };
    });

    return [
        {
            name: "listAccountTemplates", public: true,
            handler: withTemplates((args) => ({
                results: !args.capability ? [...deps.templates()] : deps.templates().filter((template) =>
                    template.capabilityProviders.some((provider) => Array.isArray(args.capability)
                        ? (args.capability as unknown[]).includes(provider.capability)
                        : provider.capability === args.capability)),
            })),
        },
        { name: "listAccounts", handler: withTemplates((args, request) => listAccounts(args, request.applicationId)) },
        {
            name: "listAccountsPublic", public: true,
            handler: withTemplates((args, request) => {
                if (!publicAccessByCapability(request.applicationId ?? "", args.capability)) {
                    throw denied();
                }
                return listAccounts(args, request.applicationId);
            }),
        },
        {
            name: "getAccountInfo", public: true,
            handler: withTemplates(async (args, request) => {
                const annotated = annotate(await getAccount(args.accountId), deps.templates());
                requirePermission("readPermissions", annotated, request);
                return { result: annotated };
            }),
        },
        { name: "createAccount", public: true, timeout: 20, handler: createAccount },
        { name: "modifyAccount", public: true, timeout: 3600, handler: modifyAccount },
        { name: "deleteAccount", public: true, timeout: 20, handler: deleteAccount },
        { name: "readCredentials", handler: readCredentials },
        {
            name: "readCredentialsPublic", public: true,
            handler: withTemplates((args, request) => {
                if (!publicAccessById(request.applicationId ?? "")) {
                    throw denied();
                }
                return (readCredentials as (r: Request) => Promise<Payload>)(request);
            }),
        },
        { name: "writeCredentials", handler: writeCredentials },
        { name: "hasCredentials", handler: withTemplates((args) => keys.has(String(args.accountId))) },
        { name: "restoreCompleted", handler: restoreCompleted },
        { name: "accountsChanged", handler: restoreCompleted },
        {
            name: "appsChanged",
            handler: withTemplates(async () => {
                await deps.reloadTemplates();
                return {};
            }),
        },
        {
            name: "setPrefs",
            handler: mojoHandler(({ payload }) => {
                if (payload.deviceName) {
                    profileDeviceName(payload.deviceName);
                }
                return {};
            }),
        },
        { name: "notifyAccountCreated", timeout: 3600, handler: notifyAccountCreated },
        { name: "notifyAccountDeleted", timeout: 3600, handler: notifyAccountDeleted },
        { name: "stayRunning", timeout: 3600, handler: stayRunning },
        { name: "createLocalAccount", public: true, timeout: 20, handler: createLocalAccount },
    ];
};
