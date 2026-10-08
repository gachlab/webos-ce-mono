// The one locale the app is shown in, kept in one place so the parts that need
// it -- the translations table (translate.ts) and the date/time field data
// (date-fields.ts) -- read the same answer.
//
// connectCard sets it once from PalmSystem.locale (AppService.locale()) before
// the first paint, so t() and the pickers are in the device's language from the
// first frame without a card having to arrange it. It also subscribes to the
// system's locale preference, so a change made in Regional Settings reaches
// every live card: useLocale notifies, connectCard repaints. That is the hot
// reload -- a card follows the language without being torn down and relaunched,
// which is what HP's cards needed because enyo read the locale once and never
// again (#19).
//
// Where the locale itself comes from -- the host, a system preference, the
// Language settings -- is #19's business and lives with the system, not a card.
// Until something sets one it is en_US, so nothing that never localizes has to
// think about it.

// HP wrote locales as language_REGION ("en_us", "es_ES"); case and separator
// varied by source. This is the default when no card has set one.
export const DEFAULT_LOCALE = "en_US";

export type LocaleListener = (locale: string) => void;

let locale = DEFAULT_LOCALE;
const listeners = new Set<LocaleListener>();

// Set the locale and, if it actually changed, tell everyone watching. A no-op
// set (the same locale) does not fire, so a card does not repaint for nothing.
export const useLocale = (value: string): void => {
    const next = value || DEFAULT_LOCALE;
    if (next === locale)
        return;
    locale = next;
    // A copy, so a listener that unsubscribes itself mid-round does not disturb
    // the iteration.
    for (const listener of [...listeners])
        listener(locale);
};

export const currentLocale = (): string => locale;

// Watch for locale changes. Returns an unsubscribe. connectCard uses this to
// repaint a card when the language changes under it; a card may use it too.
export const onLocaleChange = (listener: LocaleListener): (() => void) => {
    listeners.add(listener);
    return () => { listeners.delete(listener); };
};
