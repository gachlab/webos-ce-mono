// This card's own strings, translated, keyed by locale. The key inside each
// locale is the English text t() is called with, so a key not translated -- or
// a locale with no table, such as en_* -- falls back to English (translate.ts).
// Spanish is the first translation this project ships (#19); more languages are
// more entries in this object.
//
// Only the strings THIS card shows are here. The language names in the list are
// the system's own (languageName from getPreferenceValues), not translated by
// us -- they already read in each language's own words.

import type { LocaleTranslations } from "@webos/api/i18n/translate.ts";

export const TRANSLATIONS: LocaleTranslations = {
    es: {
        "Regional Settings": "Configuración regional",
        "Language": "Idioma",
        "Country": "País",
        "Changing language...": "Cambiando el idioma...",
        "That language is no longer available.": "Ese idioma ya no está disponible.",
        "Could not read the available languages.": "No se pudieron leer los idiomas disponibles.",
        "Could not change the language.": "No se pudo cambiar el idioma.",
    },
};
