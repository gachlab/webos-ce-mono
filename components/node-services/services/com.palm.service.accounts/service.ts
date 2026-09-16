// com.palm.service.accounts: wiring. Rewritten from HP's accounts.js.

import { createDb8 } from "#kit/db8.ts";
import type { Bus, BusOptions, Payload } from "#kit/luna.ts";
import { DEFAULT_IDLE_MS, registerCommands, type Buses } from "#kit/mojoservice.ts";
import { type Schema } from "#kit/json-schema.ts";
import { type Template } from "./accounts.ts";
import { createCommands, HP_DELAYS, SELF, type Delays } from "./commands.ts";
import { createKeyStore } from "./credentials.ts";
import { createTemplateLoader, type TemplateFiles } from "./templates.ts";

export const SERVICE_NAME = "com.palm.service.accounts";
const SIGNALING_KIND = "com.palm.signaling:1";
const TEMPLATES_APP = "com.palm.accounts.templates";

export interface AccountsDeps {
    readonly openBus: (name: string, options: BusOptions) => Bus;
    readonly createActivity: () => NonNullable<BusOptions["activity"]>;
    readonly files: TemplateFiles;
    readonly templateRoots: readonly string[];
    readonly schemas: { readonly file: Schema; readonly template: Schema };
    readonly sleep: (ms: number) => Promise<void>;
    readonly delays?: Delays;
    readonly idleMs?: number;
    readonly markProfileCreated: () => void;
    readonly exit: () => void;
    readonly log: (message: string) => void;
}

interface LocalePrefs {
    languageCode?: string;
    countryCode?: string;
}

const localeOf = (prefs: Payload): string | undefined => {
    const locale = prefs.locale as LocalePrefs | undefined;
    return locale?.languageCode && locale.countryCode ? `${locale.languageCode}_${locale.countryCode}` : undefined;
};

export interface RunningService {
    readonly buses: Buses;
    // Closes both buses and forgets the idle timer.
    readonly close: () => void;
}

export const createAccountsService = (deps: AccountsDeps) => async (): Promise<RunningService> => {
    const activity = deps.createActivity();
    const buses: Buses = {
        private: deps.openBus(SERVICE_NAME, { activity }),
        public: deps.openBus(SERVICE_NAME, { public: true, activity }),
    };
    const bus = buses.private;
    const db = createDb8(bus);
    const tempdb = createDb8(bus, "com.palm.tempdb");
    const loadTemplates = createTemplateLoader({
        files: deps.files, roots: deps.templateRoots,
        fileSchema: deps.schemas.file, templateSchema: deps.schemas.template, log: deps.log,
    });

    const state: { templates: readonly Template[]; locale: string } = { templates: [], locale: "en_us" };

    // Apps watch tempdb to learn that the list of templates changed (an app
    // with its own template was installed or removed).
    const announceTemplates = async () => {
        const ids = state.templates.map((template) => template.templateId).sort().toString();
        const query = { from: SIGNALING_KIND, where: [{ prop: "appId", op: "=", val: TEMPLATES_APP }] };
        const { results } = await tempdb.find(query);
        if (results[0]?.templates === ids) {
            deps.log(`There are ${state.templates.length} templates, same as before.`);
            return;
        }
        deps.log(`There are ${state.templates.length} templates. A template was added or deleted`);
        await tempdb.delWhere(query);
        await tempdb.put([{ _kind: SIGNALING_KIND, appId: TEMPLATES_APP, templates: ids }]);
    };

    const reloadTemplates = async () => {
        state.templates = loadTemplates(state.locale);
        void announceTemplates().catch((error: unknown) => deps.log(`announcing templates failed: ${String(error)}`));
    };

    const ready = (async () => {
        const prefs = await bus.call("luna://com.palm.systemservice/getPreferences", { keys: ["locale"] })
            .catch(() => ({}));
        state.locale = localeOf(prefs) ?? state.locale;
        await reloadTemplates();
    })();

    registerCommands(buses, createCommands({
        bus, db, tempdb,
        keys: createKeyStore(db),
        templates: () => state.templates,
        reloadTemplates,
        ready,
        sleep: deps.sleep,
        delays: deps.delays ?? HP_DELAYS,
        markProfileCreated: deps.markProfileCreated,
        log: deps.log,
    }), deps.exit);

    await ready;

    // Templates are localized when read, so a new locale means a restart.
    void (async () => {
        for await (const prefs of bus.subscribe("luna://com.palm.systemservice/getPreferences", { keys: ["locale"] })) {
            const locale = localeOf(prefs);
            if (locale && locale !== state.locale) {
                deps.log(`Restarting service because of locale change from ${state.locale} to ${locale}`);
                await bus.call(`${SELF}/__quit`).catch(() => deps.exit());
                return;
            }
        }
    })().catch((error: unknown) => deps.log(`watching the locale failed: ${String(error)}`));

    // Started on demand, the service would otherwise pay its startup on every
    // call; HP's kept itself up for an hour this way.
    void bus.call(`${SELF}/stayRunning`, { seconds: "3500" }).catch(() => undefined);
    activity.exitWhenIdle(deps.idleMs ?? DEFAULT_IDLE_MS, deps.exit);
    return {
        buses,
        close: () => {
            buses.private.close();
            buses.public.close();
            activity.stop();
        },
    };
};
