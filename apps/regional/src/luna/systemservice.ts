// com.palm.systemservice, the preferences HP's Regional Settings reads and
// writes: the locale (language + country), the region (date/number/currency
// formats) and the time format.
//
// This is the typed adapter -- every bus call lives here, the service never
// calls luna directly (the pattern apps/wifi/src/luna/wifi.ts and
// apps/dateandtime/src/luna/dateandtime.ts follow). The payload shapes are
// HP's, confirmed against the real app (com.palm.app.languagepicker) and the
// handler that answers it (components/luna-sysservice LocalePrefsHandler):
//
//   getPreferenceValues {key:"locale"} -> {locale:[{languageName, languageCode,
//                                           countries:[{countryName, countryCode}]}]}
//   getPreferenceValues {key:"region"} -> {region:[{countryName, countryCode}]}
//   getPreferences {keys:["locale","region"]} -> {locale:{languageCode,
//                                           countryCode, phoneRegion?}, region?}
//   setPreferences {locale:{languageCode, countryCode, phoneRegion?}, region?,
//                   timeFormat?}
//
// The locale value is an OBJECT, not the "xx_yy" string; that string is only a
// convenience the C++ side derives. setPreferences validates against
// /etc/palm/locale.txt and, on success, the system relaunches the shell and the
// app runners so they re-read the locale (the ProcessKiller model) -- so this
// card does not have to refresh anyone; it writes and the system carries it.

import type { LunaService, Payload } from "@webos/api/infra/luna/service.ts";

// One language the device offers, with the countries it is spoken in.
export interface Language {
    readonly languageCode: string;   // ISO 639, e.g. "es"
    readonly languageName: string;   // as the system named it, e.g. "Español"
    readonly countries: readonly Country[];
}

export interface Country {
    readonly countryCode: string;    // ISO 3166, e.g. "ES"
    readonly countryName: string;    // e.g. "España"
}

// The locale now in force: the language and country, and the phone region kept
// alongside it (used by the dialer; preserved verbatim when we write a locale
// so a language change does not drop it).
export interface CurrentLocale {
    readonly languageCode: string;
    readonly countryCode: string;
    readonly phoneRegion?: Payload;
}

export interface SystemServiceClient {
    // The languages the device offers, for the picker.
    listLanguages(): Promise<Language[]>;
    // The regions (countries) offered for date/number/currency formats.
    listRegions(): Promise<Country[]>;
    // The locale in force now, or undefined if the system has none set.
    currentLocale(): Promise<CurrentLocale | undefined>;
    // Write a new locale. The system validates, persists and relaunches.
    setLocale(locale: CurrentLocale): Promise<void>;
}

const text = (value: unknown): string => (typeof value === "string" ? value : "");

// --- pure parsers, exported so a test pins them without the bus --------------

// One language entry out of a getPreferenceValues reply, dropping anything that
// is not shaped like a language (no code, no country list).
export const languageOf = (raw: Payload): Language | undefined => {
    const languageCode = text(raw.languageCode);
    if (!languageCode)
        return undefined;
    const countriesRaw = Array.isArray(raw.countries) ? raw.countries : [];
    const countries = countriesRaw
        .map((c) => countryOf(c as Payload))
        .filter((c): c is Country => c !== undefined);
    return {
        languageCode,
        languageName: text(raw.languageName) || languageCode,
        countries,
    };
};

export const countryOf = (raw: Payload): Country | undefined => {
    const countryCode = text(raw.countryCode);
    if (!countryCode)
        return undefined;
    return { countryCode, countryName: text(raw.countryName) || countryCode };
};

// The locale object out of a getPreferences reply.
export const localeOf = (raw: unknown): CurrentLocale | undefined => {
    if (!raw || typeof raw !== "object")
        return undefined;
    const obj = raw as Payload;
    const languageCode = text(obj.languageCode);
    const countryCode = text(obj.countryCode);
    if (!languageCode || !countryCode)
        return undefined;
    const phoneRegion = (obj.phoneRegion && typeof obj.phoneRegion === "object")
        ? obj.phoneRegion as Payload
        : undefined;
    return { languageCode, countryCode, ...(phoneRegion ? { phoneRegion } : {}) };
};

// The "xx_yy" tag the kit's i18n (locale.ts / date-fields.ts) resolves against,
// built from a locale object the way HP's C++ did: language_COUNTRY. Lower-case
// language, as the dictionaries and the date-fields table are keyed.
export const localeTag = (locale: CurrentLocale): string =>
    `${locale.languageCode.toLowerCase()}_${locale.countryCode.toLowerCase()}`;

// --- the client --------------------------------------------------------------

export const createSystemService = (luna: LunaService): SystemServiceClient => {
    const base = "luna://com.palm.systemservice/";

    return {
        async listLanguages() {
            const reply = await luna.call(`${base}getPreferenceValues`, { key: "locale" });
            const list = Array.isArray(reply.locale) ? reply.locale : [];
            return list
                .map((item) => languageOf(item as Payload))
                .filter((l): l is Language => l !== undefined);
        },

        async listRegions() {
            const reply = await luna.call(`${base}getPreferenceValues`, { key: "region" });
            const list = Array.isArray(reply.region) ? reply.region : [];
            return list
                .map((item) => countryOf(item as Payload))
                .filter((c): c is Country => c !== undefined);
        },

        async currentLocale() {
            const reply = await luna.call(`${base}getPreferences`, { keys: ["locale"] });
            return localeOf(reply.locale);
        },

        async setLocale(locale) {
            await luna.call(`${base}setPreferences`, { locale });
        },
    };
};
