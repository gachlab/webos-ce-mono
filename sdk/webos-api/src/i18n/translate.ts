// What the user reads, in their language.
//
// The key is the English text, so a missing translation shows English rather
// than "settings.wifi.title". A card hands over its translations keyed by
// locale, and t() reads the one for the locale now in force (locale.ts) --
// English, the key itself, whenever that locale has no table or no entry.
//
//     useTranslations({ es: { "Turn on Wi-Fi": "Encender el Wi-Fi" } });
//     t("Turn on Wi-Fi")                 // "Encender el Wi-Fi" on es_*, English on en_*
//     t("Joining #{name}...", { name: "home" })
//
// Resolving by locale is the same rule date-fields.ts follows: the whole tag
// first ("es_es"), then its language ("es"), then English. So a card ships one
// table per language and does not care which region the device is set to.

import { currentLocale } from "./locale.ts";

// One language's strings: the English key to its translation.
export type Translations = Record<string, string>;
// What a card registers: a table per locale tag ("es", "es_ES", ...). The key
// is lower-cased on the way in, so "es_ES" and "es-es" land together.
export type LocaleTranslations = Record<string, Translations>;

// By locale tag, lower-cased. English is never a key: it is the fallback t()
// returns when a locale has no entry, which is the key itself.
let tables: LocaleTranslations = Object.create(null) as LocaleTranslations;

const empty: Translations = Object.create(null) as Translations;

// "es-ES", "es_ES", "ES" all mean the same lookup: lower case, "-" as "_" --
// the same normalization date-fields.ts uses.
const normalize = (locale: string): string => locale.toLowerCase().replace(/-/g, "_");

export const useTranslations = (translations: LocaleTranslations): void => {
    const next = Object.create(null) as LocaleTranslations;
    for (const [locale, table] of Object.entries(translations)) {
        next[normalize(locale)] = Object.assign(Object.create(null) as Translations, table);
    }
    tables = next;
};

// The table for the current locale: its whole tag, then its language, then
// nothing (English). The same most-specific-first resolution as date-fields.
const tableFor = (): Translations => {
    const tag = normalize(currentLocale());
    const language = tag.split("_")[0]!;
    return tables[tag] ?? tables[language] ?? empty;
};

// HP's own placeholder syntax, the one its strings are written in.
const fill = (text: string, values: Record<string, unknown>): string =>
    text.replace(/#\{(\w+)\}/g, (whole, key: string) =>
        (key in values ? String(values[key]) : whole));

export const t = (text: string, values?: Record<string, unknown>): string => {
    const translated = tableFor()[text] ?? text;
    return values ? fill(translated, values) : translated;
};
