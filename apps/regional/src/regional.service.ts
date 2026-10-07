// Regional Settings: the card that chooses the device's language, the way HP's
// com.palm.app.languagepicker did. Reimplemented on our kit (TypeScript, the
// Onyx theme) rather than ported -- HP's app was never released as source.
//
// The state machine and the pure logic live here; the view is main.ts and
// every bus call is src/luna/systemservice.ts. HP's app had three scenes
// (language, region formats, keyboard); this is the language half of #19, so it
// has two screens -- the list of languages, and, for a language spoken in more
// than one country, the choice of country.
//
// What it does when a language is chosen:
//   * builds the new locale object, keeping the phoneRegion the system already
//     had (a language change must not drop the dialer's region);
//   * writes it with setPreferences, which the system validates, persists, and
//     then acts on by relaunching the shell and the app runners so they re-read
//     the locale (HP's ProcessKiller model) -- so this card does not refresh
//     anyone;
//   * tells the kit's own i18n the new locale with useLocale, so this card's
//     strings and dates follow immediately for the moment before the relaunch.

import { createState, type State } from "@webos/api/helpers/create-state.ts";
import { createNavigation, type Navigation } from "@webos/api/services/navigation.service.ts";
import { useLocale } from "@webos/api/i18n/locale.ts";
import {
    localeTag,
    type CurrentLocale,
    type Language,
    type SystemServiceClient,
} from "./luna/systemservice.ts";

// The screens, as a navigation stack. "language" is the list; "country" is the
// drawer of countries for a language with more than one.
export type Screen =
    | { readonly name: "language" }
    | { readonly name: "country"; readonly languageCode: string };

export interface RegionalData {
    readonly languages: readonly Language[];
    readonly current?: CurrentLocale;
    readonly screen: Screen;
    readonly busy: boolean;
}

export interface RegionalService {
    getState(): State<RegionalData>;
    onStateChange(listener: (state: State<RegionalData>) => void): () => void;
    onShown(): void;
    onBack(): boolean;
    dispose(): void;
    // UI intents.
    openLanguage(languageCode: string): void;
    chooseLanguage(languageCode: string): void;
    chooseCountry(languageCode: string, countryCode: string): void;
    closeMenu(): void;
    now(): Screen;
}

export interface RegionalDeps {
    readonly system: SystemServiceClient;
    readonly log?: (message: string) => void;
}

// --- pure logic, exported so a test pins it without the bus ------------------

// The language with this code, or undefined.
export const languageByCode = (
    languages: readonly Language[],
    languageCode: string,
): Language | undefined => languages.find((l) => l.languageCode === languageCode);

// Whether choosing this language needs the country drawer: a language spoken in
// more than one country. One country (or none) is applied straight away, which
// is what HP's app did.
export const needsCountryChoice = (language: Language | undefined): boolean =>
    (language?.countries.length ?? 0) > 1;

// The locale a chosen language+country becomes, preserving the phoneRegion the
// current locale carried. This is what gets written.
export const localeFor = (
    languageCode: string,
    countryCode: string,
    current: CurrentLocale | undefined,
): CurrentLocale => ({
    languageCode,
    countryCode,
    ...(current?.phoneRegion ? { phoneRegion: current.phoneRegion } : {}),
});

// Whether a chosen locale is a real change from the current one. Writing the
// same locale would relaunch the shell for nothing, so a no-op is refused --
// the same guard HP's app applied before setPreferences.
export const isChange = (next: CurrentLocale, current: CurrentLocale | undefined): boolean =>
    !current
    || next.languageCode.toLowerCase() !== current.languageCode.toLowerCase()
    || next.countryCode.toLowerCase() !== current.countryCode.toLowerCase();

// Whether a language row shows a checkmark: it is the current language.
export const isCurrentLanguage = (
    language: Language,
    current: CurrentLocale | undefined,
): boolean => !!current && language.languageCode.toLowerCase() === current.languageCode.toLowerCase();

// Whether a country row shows a checkmark: it is the current country, under the
// current language.
export const isCurrentCountry = (
    languageCode: string,
    countryCode: string,
    current: CurrentLocale | undefined,
): boolean =>
    !!current
    && languageCode.toLowerCase() === current.languageCode.toLowerCase()
    && countryCode.toLowerCase() === current.countryCode.toLowerCase();

// --- the service -------------------------------------------------------------

export const createRegionalService = (deps: RegionalDeps): RegionalService => {
    const log = deps.log ?? (() => {});
    const nav: Navigation<Screen> = createNavigation<Screen>({ name: "language" });

    const state = createState<RegionalData>({
        name: "regional:loading",
        data: { languages: [], screen: nav.now(), busy: false },
    });

    // Keep the view's screen in step with the navigation stack.
    nav.onChange(() => state.patch({ screen: nav.now() }));

    // One load at a time, and only one ever: onShown fires on both activated
    // and relaunched, and two concurrent getPreferences would let the slower
    // read win and show a stale locale.
    let loading = false;
    let loaded = false;
    // One write at a time: a second tap before setPreferences resolves would
    // relaunch the shell twice. busy in the state drives the spinner; this
    // guards the path itself.
    let applying = false;

    const load = async (): Promise<void> => {
        if (loading || loaded)
            return;
        loading = true;
        try {
            const [languages, current] = await Promise.all([
                deps.system.listLanguages(),
                deps.system.currentLocale(),
            ]);
            // The locale the kit draws in follows the system's, right away.
            if (current)
                useLocale(localeTag(current));
            state.set({
                name: "regional:ready",
                data: { languages, ...(current ? { current } : {}), screen: nav.now(), busy: false },
            });
            loaded = true;
        } catch (error) {
            log(`regional: could not load languages: ${String(error)}`);
            state.set({
                name: "regional:ready",
                data: { languages: [], screen: nav.now(), busy: false },
                error: "Could not read the available languages.",
            });
        } finally {
            loading = false;
        }
    };

    const apply = async (next: CurrentLocale): Promise<void> => {
        if (applying)
            return; // a write is already in flight; ignore the second tap
        const current = state.get().data.current;
        if (!isChange(next, current)) {
            // No change: just go back to the list without touching the system.
            while (nav.back()) { /* pop to the language list */ }
            return;
        }
        applying = true;
        state.patch({ busy: true }, "regional:applying");
        try {
            await deps.system.setLocale(next);
            // Follow it in the kit now; the system will relaunch the shell and
            // the app runners to pick it up everywhere else.
            useLocale(localeTag(next));
            state.patch({ current: next, busy: false }, "regional:ready");
            while (nav.back()) { /* back to the list */ }
        } catch (error) {
            log(`regional: could not set locale: ${String(error)}`);
            // Back to the list first, so a failed apply does not strand the
            // card on a country screen whose language the view still trusts.
            while (nav.back()) { /* back to the list */ }
            state.patch({ busy: false }, "regional:ready");
            state.set({ ...state.get(), error: "Could not change the language." });
        } finally {
            applying = false;
        }
    };

    return {
        getState: state.get,
        onStateChange: state.subscribe,
        now: nav.now,

        onShown() {
            void load();
        },

        onBack() {
            return nav.back();
        },

        dispose() {
            state.clear();
        },

        // A language row tapped: with one country apply it, with several open
        // the country drawer.
        chooseLanguage(languageCode) {
            const language = languageByCode(state.get().data.languages, languageCode);
            if (!language)
                return;
            if (needsCountryChoice(language)) {
                nav.open({ name: "country", languageCode });
                return;
            }
            const country = language.countries[0];
            if (!country)
                return;
            void apply(localeFor(languageCode, country.countryCode, state.get().data.current));
        },

        // Explicit open of the country drawer (the view may call this for a
        // disclosure arrow rather than the whole row).
        openLanguage(languageCode) {
            const language = languageByCode(state.get().data.languages, languageCode);
            if (language && needsCountryChoice(language))
                nav.open({ name: "country", languageCode });
        },

        chooseCountry(languageCode, countryCode) {
            void apply(localeFor(languageCode, countryCode, state.get().data.current));
        },

        closeMenu() {
            /* reserved for the app menu; nothing pending */
        },
    };
};
