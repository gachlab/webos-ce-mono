// The one locale the app is shown in, kept in one place so the parts that need
// it -- the translations table (translate.ts) and the date/time field data
// (date-fields.ts) -- read the same answer.
//
// connectCard sets it once from PalmSystem.locale (AppService.locale()) before
// the first paint, so t() and the pickers are in the device's language from the
// first frame without a card having to arrange it. A card that changes the
// locale live (Regional Settings) calls useLocale again.
//
// Where the locale itself comes from -- the host, a system preference, the
// Language settings -- is #19's business and lives with the system, not a card.
// Until something sets one it is en_US, so nothing that never localizes has to
// think about it.

// HP wrote locales as language_REGION ("en_us", "es_ES"); case and separator
// varied by source. This is the default when no card has set one.
export const DEFAULT_LOCALE = "en_US";

let locale = DEFAULT_LOCALE;

export const useLocale = (value: string): void => {
    locale = value || DEFAULT_LOCALE;
};

export const currentLocale = (): string => locale;
