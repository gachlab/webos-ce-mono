// What the date and time pickers need to draw a locale's calendar: the month
// names it shows, the order it puts month/day/year in, whether its clock is
// 24-hour, which day its week starts on, and what it calls the halves of a
// 12-hour day.
//
// enyo carried this per locale in its g11n data -- month names in
// datetime_data/<locale>.json (the "long" set the pickers show), and
// dateFieldOrder / is12HourDefault / firstDayOfWeek in formats/<region>.json.
// enyo is gone from a card's world, so the few locales we ship are transcribed
// here from that same data rather than loaded from it. English is the source
// and the fallback: a locale we have not added yet shows en_US, never a broken
// picker.
//
// This is the date/time half of #19. The string half is translate.ts; the
// locale itself is locale.ts. Adding a language is adding an entry here and its
// translations there -- no code changes.

import { currentLocale, DEFAULT_LOCALE } from "./locale.ts";

// Which wheels the date picker shows, and in which order. A permutation of the
// three; the picker's re-clamping and its YYYY-MM-DD wire value do not depend
// on it.
export type DateField = "m" | "d" | "y";

export interface DateFields {
    // Month names as the picker shows them, January-first (index 0 = January),
    // so a caller indexes by month number regardless of display order.
    readonly months: readonly string[];
    // The order the month/day/year wheels appear in.
    readonly order: readonly DateField[];
    // The clock the locale keeps. false is 12-hour with am/pm; the wire value
    // is 24-hour either way.
    readonly is24: boolean;
    // 0 = Sunday, 1 = Monday. What a month view starts its weeks on.
    readonly firstDayOfWeek: number;
    // The 12-hour day's halves, for the am/pm wheel.
    readonly am: string;
    readonly pm: string;
}

const EN_US: DateFields = {
    months: [
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December",
    ],
    order: ["m", "d", "y"],
    is24: false,
    firstDayOfWeek: 0,
    am: "AM",
    pm: "PM",
};

const ES: DateFields = {
    months: [
        "enero", "febrero", "marzo", "abril", "mayo", "junio",
        "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre",
    ],
    order: ["d", "m", "y"],
    is24: true,
    firstDayOfWeek: 1,
    am: "AM",
    pm: "PM",
};

// By most specific first: a full locale ("es_ES"), then its language ("es").
// English is neither key nor fallback by name -- it is the default the resolver
// returns when nothing matches.
const TABLE: Record<string, DateFields> = {
    es: ES,
    es_es: ES,
    en: EN_US,
    en_us: EN_US,
};

// "es-ES", "es_ES", "ES" all mean the same lookup: lower case, "-" as "_".
const normalize = (locale: string): string => locale.toLowerCase().replace(/-/g, "_");

// The fields for a locale, trying the whole tag then its language, and falling
// back to en_US. `dateFields()` with no argument reads the current locale, so a
// picker need not thread it through.
export const dateFieldsFor = (locale: string): DateFields => {
    const tag = normalize(locale);
    const language = tag.split("_")[0]!;
    return TABLE[tag] ?? TABLE[language] ?? EN_US;
};

export const dateFields = (): DateFields => dateFieldsFor(currentLocale());

// So a caller can name the fallback without reaching for the locale string.
export { DEFAULT_LOCALE };
