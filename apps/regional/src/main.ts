// Regional Settings, drawn with the kit. Two screens, both a function of the
// state in regional.service.ts: the list of languages, and the countries of a
// language spoken in more than one. Nothing here decides anything; it draws
// what the service says and tells it what the user did.
//
// The language names are the system's own (languageName from
// getPreferenceValues), so they read in each language's own words -- "Español",
// not "Spanish" -- the way HP's picker showed them.

import { openBus } from "@webos/api/infra/luna/open-bus.ts";
import { t } from "@webos/api/i18n/translate.ts";
import { useTranslations } from "@webos/api/i18n/translate.ts";
import { useLocale } from "@webos/api/i18n/locale.ts";
import { watchSystemLocale } from "@webos/api/i18n/watch-locale.ts";
import { html } from "@webos/ui-kit/element.ts";
import { error as errorLine } from "@webos/ui-kit/kit/kit.ts";
import { startCard } from "@webos/ui-kit/start-card.ts";
import { createRegionalService, isCurrentCountry, isCurrentLanguage,
         languageByCode, type RegionalData, type RegionalService } from "./regional.service.ts";
import { createSystemService } from "./luna/systemservice.ts";
import { TRANSLATIONS } from "./translations.ts";
import type { State } from "@webos/api/helpers/create-state.ts";

// This card's translations, keyed by locale: t() now reads the one for the
// locale the device is in (connect-card set it from PalmSystem.locale before
// the first paint), falling back to English. Registering at module load is
// safe -- t() resolves lazily at render, by which time the locale is set.
useTranslations(TRANSLATIONS);

const languageList = (data: RegionalData, service: RegionalService) => html`
    <div class="wos-group">
        <div class="wos-group-title">${t("Language")}</div>
        <div class="wos-list">
            ${data.languages.map((language) => html`
                <wos-row
                    title=${language.languageName}
                    ?strong=${isCurrentLanguage(language, data.current)}
                    @select=${() => service.chooseLanguage(language.languageCode)}>
                    ${isCurrentLanguage(language, data.current)
                        ? html`<span slot="lead" class="check" aria-hidden="true"></span>`
                        : null}
                </wos-row>`)}
        </div>
    </div>`;

const countryList = (data: RegionalData, service: RegionalService, languageCode: string) => {
    const language = languageByCode(data.languages, languageCode);
    if (!language)
        return html`${errorLine(t("That language is no longer available."))}`;
    return html`
        <div class="wos-group">
            <div class="wos-group-title">${language.languageName}</div>
            <div class="wos-list">
                ${language.countries.map((country) => html`
                    <wos-row
                        title=${country.countryName}
                        ?strong=${isCurrentCountry(languageCode, country.countryCode, data.current)}
                        @select=${() => service.chooseCountry(languageCode, country.countryCode)}>
                        ${isCurrentCountry(languageCode, country.countryCode, data.current)
                            ? html`<span slot="lead" class="check" aria-hidden="true"></span>`
                            : null}
                    </wos-row>`)}
            </div>
        </div>`;
};

const view = (state: State<RegionalData>, service: RegionalService) => {
    const data = state.data;
    const onLanguageList = data.screen.name === "language";
    const title = onLanguageList ? t("Regional Settings") : t("Country");
    // The first read: nothing to choose from yet. Show a spinner rather than an
    // empty list the user could tap into nothing.
    const loading = state.name === "regional:loading" && data.languages.length === 0 && !state.error;
    const body = loading
        ? html`<wos-spinner label=${t("Loading...")}></wos-spinner>`
        : onLanguageList
            ? languageList(data, service)
            : countryList(data, service, data.screen.languageCode);

    return html`
        <div class="wos-card">
            <wos-header title=${title} ?back=${!onLanguageList} light
                        @back=${() => service.onBack()}>
            </wos-header>
            <div class="wos-body">
                ${state.error ? errorLine(t(state.error)) : null}
                ${data.busy
                    ? html`<wos-spinner label=${t("Changing language...")}></wos-spinner>`
                    : body}
            </div>
        </div>`;
};

const luna = openBus({
    // Development only: the fake bus answers these so the card shows real data
    // in a plain browser. tools/build-cards.sh drops this whole branch (and the
    // fake bus) from the installed card -- see open-bus.ts.
    "luna://com.palm.systemservice/getPreferenceValues": () => ({
        returnValue: true,
        locale: [
            { languageName: "English", languageCode: "en", countries: [
                { countryName: "United States", countryCode: "US" },
                { countryName: "United Kingdom", countryCode: "GB" },
            ] },
            { languageName: "Español", languageCode: "es", countries: [
                { countryName: "España", countryCode: "ES" },
                { countryName: "México", countryCode: "MX" },
            ] },
            { languageName: "Français", languageCode: "fr", countries: [
                { countryName: "France", countryCode: "FR" },
            ] },
            { languageName: "日本語", languageCode: "ja", countries: [
                { countryName: "日本", countryCode: "JP" },
            ] },
        ],
    }),
    "luna://com.palm.systemservice/getPreferences": () => ({
        returnValue: true,
        locale: { languageCode: "en", countryCode: "US" },
    }),
    "luna://com.palm.systemservice/setPreferences": () => ({ returnValue: true }),
});

const service = createRegionalService({
    system: createSystemService(luna),
    log: (message) => console.warn(message),
});

startCard({ service, view });

// Follow the system locale while the card is open, so if the language is
// changed anywhere -- including by this card -- t() and the date pickers across
// the kit stay in step without a relaunch. connectCard repaints on the change.
watchSystemLocale(luna).start();

// Development only: a hook to drive the locale from the console or a probe, to
// see the hot reload without a running shell. The bundler drops this when
// WEBOS_CARDS_DEV is false (see open-bus.ts), so it is not in the shipped card.
declare const WEBOS_CARDS_DEV: boolean | undefined;
if (typeof WEBOS_CARDS_DEV !== "undefined" && WEBOS_CARDS_DEV) {
    (globalThis as { __wosDev?: unknown }).__wosDev = { setLocale: useLocale };
}
