// The one locale the app is shown in, kept in one place so the parts that need
// it -- the translations table (translate.ts) and the date/time field data
// (date-fields.ts) -- read the same answer.
//
// A card sets it once, from PalmSystem.locale (AppService.locale()), the way it
// hands over its translations with useTranslations:
//
//     useLocale(app.locale());   // e.g. "es_ES"
//
// Where the locale itself comes from -- the host, a system preference, the
// Language settings -- is #19's business and lives with the system, not a card.
// Until a card says otherwise it is en_US, so nothing that never localizes has
// to think about it.

// HP wrote locales as language_REGION ("en_us", "es_ES"); case and separator
// varied by source. This is the default when no card has set one.
export const DEFAULT_LOCALE = "en_US";

let locale = DEFAULT_LOCALE;

export const useLocale = (value: string): void => {
    locale = value || DEFAULT_LOCALE;
};

export const currentLocale = (): string => locale;
