// What the user reads, in their language.
//
// The key is the English text, so a missing translation shows English rather
// than "settings.wifi.title". Translations arrive as a flat object; where they
// come from is #19's business, not a card's.
//
//     t("Turn on Wi-Fi")
//     t("Joining #{name}...", { name: "home" })

export type Translations = Record<string, string>;

// Without a prototype: t("constructor") must be the word, not a function.
let table: Translations = Object.create(null) as Translations;

export const useTranslations = (translations: Translations): void => {
    table = Object.assign(Object.create(null) as Translations, translations);
};

// HP's own placeholder syntax, the one its strings are written in.
const fill = (text: string, values: Record<string, unknown>): string =>
    text.replace(/#\{(\w+)\}/g, (whole, key: string) =>
        (key in values ? String(values[key]) : whole));

export const t = (text: string, values?: Record<string, unknown>): string => {
    const translated = table[text] ?? text;
    return values ? fill(translated, values) : translated;
};
