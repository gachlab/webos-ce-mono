// Who may have the position, and asking the user when that is not settled.
//
// HP's flow, which the system UI still implements: the service publishes
// registerForLocationServiceNotifications to it (com.palm.systemmanager/
// publishToSystemUI), the system UI opens its "Location Services" alert, and
// the alert answers with acceptLocationRequest, acceptAlwaysLocationRequest,
// rejectLocationRequest -- or ignoreLocationRequest when it is closed without
// a tap -- carrying {appId} for an application or {url} for a website.
//
// * An application is asked about once a session, unless Auto Locate is on.
// * A website is asked about unless the user always allows it, and never
//   gets the position with "Never Share Location".
// * The system itself -- a caller that is not an application -- is not
//   asked.
//
// The alert opens one at a time (the system UI ignores a second while one is
// up), so the questions queue.

import { lunaError, type Payload } from "#kit/luna.ts";
import { ERRORS } from "./locator.ts";
import type { PrefsStore } from "./prefs.ts";

export type Asker =
    | { readonly kind: "system" }
    | { readonly kind: "app"; readonly appId: string }
    | { readonly kind: "web"; readonly url: string; readonly name: string };

export type Answer = "accept" | "acceptAlways" | "reject" | "ignore";

export interface Consent {
    // Resolves when the asker may have the position; otherwise throws HP's
    // error.
    check(asker: Asker, signal: AbortSignal): Promise<void>;
    // What the alert said. `key` is the appId or url it was opened for.
    answer(answer: Answer, key: string): void;
    // The per-site decisions are forgotten ("Clear My Location Data").
    clearSites(): void;
}

export interface ConsentDeps {
    readonly prefs: PrefsStore;
    readonly publish: (payload: Payload) => Promise<void>;
    readonly log: (message: string) => void;
    // How long an alert may go unanswered; setTimeout when left out.
    readonly later?: (callback: () => void, ms: number) => { cancel(): void };
    readonly questionMs?: number;
}

// An alert closed some other way than its buttons may never say so, and the
// questions behind it would wait for good: after this, it counts as ignored.
export const QUESTION_MS = 60_000;

// The shell's own pages; HP's service let its shell through as well.
export const SYSTEM_APPS: ReadonlySet<string> = new Set(["com.palm.luna", "com.palm.launcher", "com.palm.systemui"]);

// A website is known by its origin, as browsers do.
export const siteOf = (url: string): string => {
    try {
        const parsed = new URL(url);
        return parsed.origin === "null" ? url : parsed.origin;
    } catch {
        return url;
    }
};

// Who is asking, from the request. An application's id comes from the hub,
// which a page cannot forge. A url is only taken from a caller that is not an
// application: WebAppMgr, asking for one of its pages.
export const askerOf = (applicationId: string | undefined, payload: Payload): Asker => {
    if (applicationId) {
        const appId = applicationId.split(" ")[0]!;
        return SYSTEM_APPS.has(appId) ? { kind: "system" } : { kind: "app", appId };
    }
    if (typeof payload.url === "string" && payload.url) {
        const site = siteOf(payload.url);
        let name = site;
        try {
            name = new URL(site).host || site;
        } catch {
            // The url itself, then.
        }
        return { kind: "web", url: site, name };
    }
    return { kind: "system" };
};

const denied = () => lunaError(ERRORS.denied, "The user did not allow the location");

interface Question {
    readonly key: string;
    readonly message: Payload;
    readonly waiters: Set<(answer: Answer) => void>;
    expiry?: { cancel(): void };
}

const defaultLater = (callback: () => void, ms: number) => {
    const timer = setTimeout(callback, ms);
    timer.unref?.();
    return { cancel: () => clearTimeout(timer) };
};

export const createConsent = (deps: ConsentDeps): Consent => {
    // This session's answers, by appId or site.
    const allowed = new Set<string>();
    const refused = new Set<string>();
    const queue: Question[] = [];

    const ask = async () => {
        const question = queue[0];
        if (!question) {
            return;
        }
        const key = question.key;
        question.expiry = (deps.later ?? defaultLater)(() => settle(key, "ignore"), deps.questionMs ?? QUESTION_MS);
        try {
            await deps.publish({ event: "registerForLocationServiceNotifications", message: question.message });
        } catch (error) {
            deps.log(`the system UI could not be asked: ${error instanceof Error ? error.message : String(error)}`);
            settle(question.key, "ignore");
        }
    };

    const settle = (key: string, answer: Answer) => {
        const index = queue.findIndex((question) => question.key === key);
        if (index < 0) {
            return;
        }
        const [question] = queue.splice(index, 1);
        question!.expiry?.cancel();
        for (const waiter of question!.waiters) {
            waiter(answer);
        }
        if (index === 0) {
            void ask();
        }
    };

    const wait = (key: string, message: Payload, signal: AbortSignal): Promise<Answer> =>
        new Promise((resolve, reject) => {
            let question = queue.find((q) => q.key === key);
            if (!question) {
                question = { key, message, waiters: new Set() };
                queue.push(question);
                if (queue.length === 1) {
                    void ask();
                }
            }
            const current = question;
            const waiter = (answer: Answer) => {
                signal.removeEventListener("abort", onAbort);
                resolve(answer);
            };
            const onAbort = () => {
                current.waiters.delete(waiter);
                reject(signal.reason);
            };
            current.waiters.add(waiter);
            signal.addEventListener("abort", onAbort, { once: true });
        });

    return {
        check: async (asker, signal) => {
            if (asker.kind === "system") {
                return;
            }
            const prefs = deps.prefs.get();
            let key: string;
            let message: Payload;
            if (asker.kind === "app") {
                if (prefs.autoLocate) {
                    return;
                }
                key = asker.appId;
                message = { appId: asker.appId };
            } else {
                if (!prefs.webSetting) {
                    throw denied();
                }
                if (prefs.allowedSites.includes(asker.url)) {
                    return;
                }
                key = asker.url;
                message = { web: { url: asker.url, name: asker.name } };
            }
            if (allowed.has(key)) {
                return;
            }
            if (refused.has(key)) {
                throw denied();
            }
            const answer = await wait(key, message, signal);
            if (answer === "accept" || answer === "acceptAlways") {
                return;
            }
            throw denied();
        },
        answer: (answer, key) => {
            if (answer === "accept" || answer === "acceptAlways") {
                allowed.add(key);
                refused.delete(key);
            } else if (answer === "reject") {
                refused.add(key);
                allowed.delete(key);
            }
            // Always Allow is only offered for websites.
            if (answer === "acceptAlways" && !deps.prefs.get().allowedSites.includes(key)) {
                deps.prefs.set({ allowedSites: [...deps.prefs.get().allowedSites, key] });
            }
            settle(key, answer);
        },
        clearSites: () => {
            const sites = deps.prefs.get().allowedSites;
            for (const site of sites) {
                allowed.delete(site);
            }
            for (const key of [...refused]) {
                if (key.includes("://")) {
                    refused.delete(key);
                }
            }
            deps.prefs.set({ allowedSites: [] });
        },
    };
};
