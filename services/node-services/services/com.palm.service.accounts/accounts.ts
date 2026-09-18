// Accounts and templates: the pure part of com.palm.service.accounts.
//
// Rewritten from HP's models/account-model.js, utils.js and publicwhitelist.js
// (components/app-services/com.palm.service.accounts), keeping their results.

import type { Payload } from "#kit/luna.ts";
import { mojoError } from "#kit/mojoservice.ts";

export interface CapabilityProvider extends Payload {
    id: string;
    capability?: string;
    // db8 gives an _id to the providers stored in an account; HP's code uses
    // it to tell the enabled ones.
    _id?: string;
    onEnabled?: string;
    onCreate?: string;
    onDelete?: string;
    onCredentialsChanged?: string;
    validator?: string;
    alwaysOn?: boolean;
}

export interface Template extends Payload {
    templateId: string;
    loc_name?: string;
    capabilityProviders: CapabilityProvider[];
    validator?: string;
    onCapabilitiesChanged?: string;
    onCredentialsChanged?: string;
    readPermissions?: string[];
    writePermissions?: string[];
}

export interface Account extends Payload {
    _id?: string;
    _kind?: string;
    _del?: boolean;
    templateId: string;
    username?: string;
    alias?: string;
    beingDeleted?: boolean;
    capabilityProviders: CapabilityProvider[];
    retries?: number;
}

export const ACCOUNT_KIND = "com.palm.account:1";
export const PROFILE_TEMPLATE = "com.palm.palmprofile";

export const findTemplate = (templates: readonly Template[], templateId: string): Template | undefined =>
    templates.find((template) => template.templateId === templateId);

// The account with its template woven in: template properties win, at the top
// level and for each of the account's providers, but the account's own extra
// properties stay. Only the account's providers are listed. Throws when the
// template is gone.
export const annotate = (account: Account, templates: readonly Template[]): Account => {
    const template = findTemplate(templates, account.templateId);
    if (!template) {
        throw new Error(`template not found: ${account.templateId}`);
    }
    const providers = (account.capabilityProviders ?? []).map((provider) => {
        const fromTemplate = template.capabilityProviders.find((candidate) => candidate.id === provider.id);
        return fromTemplate ? { ...provider, ...fromTemplate } : provider;
    });
    return { ...account, ...template, capabilityProviders: providers } as Account;
};

// Every provider of the template, and whether it is among `enabled`.
export const providerStates = (template: Template, enabled: readonly { id: string }[]) =>
    template.capabilityProviders.map((provider) => ({
        id: provider.id,
        enabled: enabled.some((candidate) => candidate.id === provider.id),
    }));

export const diffLists = <T>(oldList: readonly T[], newList: readonly T[]) => ({
    add: newList.filter((item) => !oldList.includes(item)),
    remove: oldList.filter((item) => !newList.includes(item)),
});

// The stored form of the providers named in `requested`: id and capability,
// taken from the template. HP's code failed on a provider the template does not
// have, and so does this.
export const storedProviders = (template: Template, requested: readonly { id: string }[]) =>
    requested.map((wanted) => {
        const provider = template.capabilityProviders.find((candidate) => candidate.id === wanted.id);
        if (!provider) {
            throw new TypeError(`Cannot read properties of undefined (reading 'id')`);
        }
        return { id: provider.id, capability: provider.capability };
    });

export const globToRegex = (glob: string): RegExp => {
    const escaped = [...glob].map((c) => {
        switch (c) {
            case "*": return ".*";
            case "?": return ".";
            case ".": return "\\.";
            case "\\": return "\\\\";
            default: return c;
        }
    }).join("");
    return new RegExp(`^${escaped}$`);
};

// A bug in the old bus reported ids as "appid processid" (DFISH-5527).
export const stripProcessNumber = (id: string): string => {
    const space = id.indexOf(" ");
    return space === -1 ? id : id.slice(0, space);
};

export interface Caller {
    readonly applicationId: string | undefined;
    readonly senderServiceName: string | undefined;
}

const ALWAYS_ALLOWED = ["com.palm.app.accounts", "com.palm.service.accounts"];

// Throws unless the caller may use the account (or template) this way. The
// accounts app and service may always, even when the template is gone, as in
// HP's Utils.hasPermission; for anyone else a missing template fails, and so
// does a caller with no name at all.
export const requirePermission = (kind: "readPermissions" | "writePermissions",
                                  subject: Account | Template | undefined, caller: Caller): void => {
    const callerId = stripProcessNumber(caller.applicationId || caller.senderServiceName || "");
    if (ALWAYS_ALLOWED.includes(callerId)) {
        return;
    }
    if (!subject) {
        throw new TypeError(`Cannot read properties of undefined (reading '${kind}')`);
    }
    if (callerId === "") {
        throw mojoError("", `Permission denied! An unnamed caller is not specified in template ${String(subject.templateId)} ${kind}`);
    }
    const permitted = subject[kind] as string[] | undefined;
    if (permitted?.some((glob) => globToRegex(glob).test(callerId))) {
        return;
    }
    throw mojoError("", `Permission denied! ${callerId} is not specified in template ${String(subject.templateId)} ${kind}`);
};

const WHITELIST = [
    { id: "com.quickoffice.webos", capability: "documents" },
    { id: "com.quickoffice.ar", capability: "documents" },
];

const whitelisted = (id: string) => WHITELIST.find((entry) => entry.id === stripProcessNumber(id));

export const publicAccessById = (id: string): boolean => whitelisted(id) !== undefined;

export const publicAccessByCapability = (id: string, capability: unknown): boolean => {
    const entry = whitelisted(id);
    return entry !== undefined && typeof capability === "string" && entry.capability === capability.toLowerCase();
};
