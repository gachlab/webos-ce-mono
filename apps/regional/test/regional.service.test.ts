// The Regional Settings card: the pure locale logic and the state machine,
// against a fake com.palm.systemservice.
//
// What is pinned: that the card reads the languages and the current locale, that
// a language with one country applies straight away while a language with
// several opens the country drawer, that a chosen locale keeps the phoneRegion
// the system had, that choosing the locale already in force writes nothing (no
// pointless shell relaunch), and that the payload sent to setPreferences is the
// {locale:{languageCode,countryCode}} shape HP's handler expects.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createFakeLuna, type FakeLuna } from "@webos/api/infra/luna/fake.service.ts";
import { currentLocale } from "@webos/api/i18n/locale.ts";
import { createSystemService, localeOf, localeTag,
         type CurrentLocale } from "../src/luna/systemservice.ts";
import { createRegionalService, isChange, localeFor, needsCountryChoice,
         languageByCode } from "../src/regional.service.ts";
import type { Payload } from "@webos/api/infra/luna/service.ts";

const SYSTEM = "luna://com.palm.systemservice/";
const settle = () => new Promise((resolve) => setImmediate(resolve));

const payloads = (luna: FakeLuna, uri: string): Payload[] =>
    luna.calls.filter((call) => call.uri === uri).map((call) => call.payload);

// A systemservice with English and Spanish; English spoken in two countries,
// Spanish in two, so both exercise the country drawer. The current locale is
// en_US with a phoneRegion the test watches is preserved.
const LANGUAGES = [
    {
        languageCode: "en", languageName: "English",
        countries: [
            { countryCode: "US", countryName: "United States" },
            { countryCode: "GB", countryName: "United Kingdom" },
        ],
    },
    {
        languageCode: "es", languageName: "Español",
        countries: [
            { countryCode: "ES", countryName: "España" },
            { countryCode: "MX", countryName: "México" },
        ],
    },
    {
        languageCode: "ja", languageName: "日本語",
        countries: [{ countryCode: "JP", countryName: "日本" }],
    },
];

const CURRENT = {
    languageCode: "en", countryCode: "US",
    phoneRegion: { countryCode: "us", countryName: "United States" },
};

const setup = (overrides?: { current?: unknown }) => {
    const luna = createFakeLuna();
    luna.answer(`${SYSTEM}getPreferenceValues`, (payload) => {
        if (payload.key === "locale")
            return { returnValue: true, locale: LANGUAGES };
        return { returnValue: true, region: [] };
    });
    luna.answer(`${SYSTEM}getPreferences`, () => ({
        returnValue: true,
        locale: "current" in (overrides ?? {}) ? overrides!.current : CURRENT,
    }));
    luna.answer(`${SYSTEM}setPreferences`, () => ({ returnValue: true }));
    const service = createRegionalService({ system: createSystemService(luna) });
    return { luna, service };
};

describe("the pure locale logic", () => {
    test("a language with more than one country needs the drawer", () => {
        assert.equal(needsCountryChoice(LANGUAGES[0]), true);  // en: US, GB
        assert.equal(needsCountryChoice(LANGUAGES[2]), false); // ja: JP only
        assert.equal(needsCountryChoice(undefined), false);
    });

    test("a chosen locale keeps the phoneRegion the current one had", () => {
        const next = localeFor("es", "ES", CURRENT);
        assert.equal(next.languageCode, "es");
        assert.equal(next.countryCode, "ES");
        assert.deepEqual(next.phoneRegion, CURRENT.phoneRegion);
    });

    test("choosing the current locale is not a change", () => {
        assert.equal(isChange({ languageCode: "en", countryCode: "US" }, CURRENT), false);
        // Case does not matter: en/US and EN/us are the same locale.
        assert.equal(isChange({ languageCode: "EN", countryCode: "us" }, CURRENT), false);
        assert.equal(isChange({ languageCode: "es", countryCode: "ES" }, CURRENT), true);
    });

    test("localeTag builds the lower-case xx_yy the kit resolves against", () => {
        assert.equal(localeTag({ languageCode: "es", countryCode: "ES" }), "es_es");
    });

    test("localeOf reads a locale object and rejects a broken one", () => {
        assert.deepEqual(localeOf({ languageCode: "es", countryCode: "ES" }),
                         { languageCode: "es", countryCode: "ES" });
        assert.equal(localeOf({ languageCode: "es" }), undefined); // no country
        assert.equal(localeOf("es_ES"), undefined);                // not an object
    });
});

describe("the card reads the system on show", () => {
    test("it lists languages and marks the current one", async () => {
        const { luna, service } = setup();
        service.onShown();
        await settle();
        const data = service.getState().data;
        assert.equal(data.languages.length, 3);
        assert.equal(data.current?.languageCode, "en");
        assert.equal(data.current?.countryCode, "US");
        // getPreferenceValues was asked for the locale list.
        assert.ok(payloads(luna, `${SYSTEM}getPreferenceValues`).some((p) => p.key === "locale"));
    });

    test("it follows the system locale in the kit's own i18n", async () => {
        const { service } = setup();
        service.onShown();
        await settle();
        // currentLocale() is a module singleton; en_US here, lower-cased tag.
        assert.equal(currentLocale(), "en_us");
    });
});

describe("choosing a language", () => {
    test("one country applies straight away", async () => {
        const { luna, service } = setup();
        service.onShown();
        await settle();
        service.chooseLanguage("ja"); // ja has one country, JP
        await settle();
        const sent = payloads(luna, `${SYSTEM}setPreferences`);
        assert.equal(sent.length, 1);
        assert.deepEqual(sent[0]!.locale, {
            languageCode: "ja", countryCode: "JP",
            phoneRegion: CURRENT.phoneRegion, // preserved
        });
    });

    test("several countries open the drawer and do not write yet", async () => {
        const { luna, service } = setup();
        service.onShown();
        await settle();
        service.chooseLanguage("es"); // es has ES and MX
        await settle();
        assert.equal(service.now().name, "country");
        assert.equal(payloads(luna, `${SYSTEM}setPreferences`).length, 0);

        service.chooseCountry("es", "MX");
        await settle();
        const sent = payloads(luna, `${SYSTEM}setPreferences`);
        assert.equal(sent.length, 1);
        assert.deepEqual(sent[0]!.locale, {
            languageCode: "es", countryCode: "MX",
            phoneRegion: CURRENT.phoneRegion,
        });
        // Applying returns to the language list.
        assert.equal(service.now().name, "language");
    });

    test("the write is {locale:{...}} -- the key HP's handler reads", async () => {
        const { luna, service } = setup();
        service.onShown();
        await settle();
        service.chooseLanguage("ja");
        await settle();
        const sent = payloads(luna, `${SYSTEM}setPreferences`);
        assert.equal(sent.length, 1);
        // The top-level key is "locale", not "localeInfo" or anything else: a
        // rename here is a silent no-op against com.palm.systemservice.
        assert.deepEqual(Object.keys(sent[0]!), ["locale"]);
        assert.ok(sent[0]!.locale, "the locale object is under the 'locale' key");
    });

    test("re-choosing the current locale writes nothing", async () => {
        const { luna, service } = setup();
        service.onShown();
        await settle();
        service.chooseCountry("en", "US"); // the current locale
        await settle();
        assert.equal(payloads(luna, `${SYSTEM}setPreferences`).length, 0);
    });
});

describe("missing current locale", () => {
    test("the card still lists languages when the system has no locale set", async () => {
        const { service } = setup({ current: undefined });
        service.onShown();
        await settle();
        const data = service.getState().data;
        assert.equal(data.languages.length, 3);
        assert.equal(data.current, undefined);
        // Any language the user picks is a change, since there is nothing to
        // compare against.
        assert.equal(isChange({ languageCode: "en", countryCode: "US" }, data.current), true);
        assert.ok(languageByCode(data.languages, "es"));
    });
});
