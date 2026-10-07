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
    <wos-group title=${t("Language")}>
        ${data.languages.map((language) => html`
            <wos-row
                title=${language.languageName}
                ?selected=${isCurrentLanguage(language, data.current)}
                @select=${() => service.chooseLanguage(language.languageCode)}>
                ${isCurrentLanguage(language, data.current)
                    ? html`<span slot="lead" class="check" aria-hidden="true"></span>`
                    : null}
            </wos-row>`)}
    </wos-group>`;

const countryList = (data: RegionalData, service: RegionalService, languageCode: string) => {
    const language = languageByCode(data.languages, languageCode);
    if (!language)
        return html`${errorLine(t("That language is no longer available."))}`;
    return html`
        <wos-group title=${language.languageName}>
            ${language.countries.map((country) => html`
                <wos-row
                    title=${country.countryName}
                    ?selected=${isCurrentCountry(languageCode, country.countryCode, data.current)}
                    @select=${() => service.chooseCountry(languageCode, country.countryCode)}>
                    ${isCurrentCountry(languageCode, country.countryCode, data.current)
                        ? html`<span slot="lead" class="check" aria-hidden="true"></span>`
                        : null}
                </wos-row>`)}
        </wos-group>`;
};

const view = (state: State<RegionalData>, service: RegionalService) => {
    const data = state.data;
    const onLanguageList = data.screen.name === "language";
    const title = onLanguageList ? t("Regional Settings") : t("Country");
    const body = onLanguageList
        ? languageList(data, service)
        : countryList(data, service, data.screen.languageCode);

    return html`
        <div class="card">
            <wos-header title=${title} ?back=${!onLanguageList} light
                        @back=${() => service.onBack()}>
            </wos-header>
            <div class="body">
                ${state.error ? errorLine(t(state.error)) : null}
                ${data.busy ? html`<wos-spinner label=${t("Changing language...")}></wos-spinner>` : body}
            </div>
        </div>`;
};

const luna = openBus();

const service = createRegionalService({
    system: createSystemService(luna),
    log: (message) => console.warn(message),
});

startCard({ service, view });
