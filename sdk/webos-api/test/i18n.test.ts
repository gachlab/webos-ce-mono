// The date/time half of #19: the locale a card sets decides the pickers' month
// names, field order and clock. The pickers themselves draw into a shadow root
// (a DOM the test runner has not got), so what is tested here is the data they
// read -- dateFieldsFor for the pure lookup, and useLocale/dateFields for the
// ambient one a picker reads at render.

import assert from "node:assert/strict";
import { afterEach, describe, test } from "node:test";

import { currentLocale, DEFAULT_LOCALE, onLocaleChange, useLocale } from "@webos/api/i18n/locale.ts";
import { dateFields, dateFieldsFor } from "@webos/api/i18n/date-fields.ts";
import { localeTagOf, watchSystemLocale } from "@webos/api/i18n/watch-locale.ts";
import { createFakeLuna } from "@webos/api/infra/luna/fake.service.ts";

// The ambient locale is module state; leave it as found so order does not
// matter and no other test inherits a locale.
afterEach(() => useLocale(DEFAULT_LOCALE));

describe("the ambient locale", () => {
    test("is en_US until a card sets one", () => {
        assert.equal(currentLocale(), "en_US");
    });

    test("is whatever a card set", () => {
        useLocale("es_ES");
        assert.equal(currentLocale(), "es_ES");
    });

    test("an empty locale falls back to the default, not to blank", () => {
        useLocale("");
        assert.equal(currentLocale(), DEFAULT_LOCALE);
    });
});

describe("the locale is observable (hot reload)", () => {
    test("a change notifies watchers; a no-op set does not", () => {
        useLocale("en_US");
        const seen: string[] = [];
        const off = onLocaleChange((l) => seen.push(l));
        useLocale("es_ES");       // a change -> fires
        useLocale("es_ES");       // the same -> does not fire
        useLocale("en_US");       // a change -> fires
        off();
        useLocale("es_ES");       // after unsubscribe -> not seen
        assert.deepEqual(seen, ["es_ES", "en_US"]);
        useLocale(DEFAULT_LOCALE);
    });

    test("an empty value that resolves to the current locale does not fire", () => {
        useLocale(DEFAULT_LOCALE);
        const seen: string[] = [];
        const off = onLocaleChange((l) => seen.push(l));
        useLocale(""); // resolves to DEFAULT_LOCALE, which is already current
        off();
        assert.deepEqual(seen, []);
    });
});

describe("date fields for a locale", () => {
    test("en_US: month names in English, month-day-year, 12-hour, week from Sunday", () => {
        const en = dateFieldsFor("en_US");
        assert.equal(en.months[0], "January");
        assert.equal(en.months[11], "December");
        assert.deepEqual(en.order, ["m", "d", "y"]);
        assert.equal(en.is24, false);
        assert.equal(en.firstDayOfWeek, 0);
    });

    test("es_ES: month names in Spanish, day-month-year, 24-hour, week from Monday", () => {
        const es = dateFieldsFor("es_ES");
        assert.equal(es.months[0], "enero");
        assert.equal(es.months[11], "diciembre");
        assert.deepEqual(es.order, ["d", "m", "y"]);
        assert.equal(es.is24, true);
        assert.equal(es.firstDayOfWeek, 1);
    });

    test("months are January-first whatever the display order, so a caller indexes by month number", () => {
        // es shows day first, but month 2 (index) is still March / marzo.
        assert.equal(dateFieldsFor("en_US").months[2], "March");
        assert.equal(dateFieldsFor("es_ES").months[2], "marzo");
    });

    test("a language with no region resolves to the language's fields", () => {
        assert.deepEqual(dateFieldsFor("es").order, ["d", "m", "y"]);
        assert.equal(dateFieldsFor("es").is24, true);
    });

    test("case and separator do not matter: es-ES, ES, es_es all mean Spanish", () => {
        for (const tag of ["es-ES", "ES", "es_es", "Es_Es"]) {
            assert.equal(dateFieldsFor(tag).months[0], "enero", tag);
        }
    });

    test("an unknown locale falls back to en_US, never a broken picker", () => {
        const unknown = dateFieldsFor("qq_ZZ");
        assert.equal(unknown.months[0], "January");
        assert.deepEqual(unknown.order, ["m", "d", "y"]);
        assert.equal(unknown.is24, false);
    });

    test("an unknown region on a known language still gets that language", () => {
        // es_AR is not a key; it must resolve through the language to Spanish,
        // not fall past it to English.
        assert.equal(dateFieldsFor("es_AR").months[0], "enero");
    });
});

describe("dateFields() reads the ambient locale", () => {
    test("follows the locale a card set, so a picker need not thread it through", () => {
        useLocale("es_ES");
        assert.deepEqual(dateFields().order, ["d", "m", "y"]);
        assert.equal(dateFields().months[0], "enero");
    });

    test("is en_US by default", () => {
        assert.equal(dateFields().months[0], "January");
        assert.equal(dateFields().is24, false);
    });
});


describe("following the system locale over the bus", () => {
    const SYSTEM = "luna://com.palm.systemservice/getPreferences";

    test("localeTagOf reads the locale object and rejects a broken one", () => {
        assert.equal(localeTagOf({ locale: { languageCode: "es", countryCode: "ES" } }), "es_es");
        assert.equal(localeTagOf({ locale: { languageCode: "es" } }), ""); // no country
        assert.equal(localeTagOf({ locale: "es_ES" }), "");                // not an object
        assert.equal(localeTagOf({}), "");                                 // no locale
    });

    test("a pushed locale change sets the kit's locale", () => {
        useLocale("en_US");
        const luna = createFakeLuna();
        luna.answer(SYSTEM, () => ({ returnValue: true, locale: { languageCode: "en", countryCode: "US" } }));
        const watch = watchSystemLocale(luna);
        watch.start();
        // the subscribe's first reply (en_US) already arrived; now the system
        // pushes a change, as it would after Regional Settings wrote it.
        const sub = luna.subscribers.find((s) => s.uri === SYSTEM);
        assert.ok(sub, "it subscribed to the locale preference");
        sub!.push({ returnValue: true, locale: { languageCode: "es", countryCode: "ES" } });
        assert.equal(currentLocale(), "es_es");
        watch.stop();
        useLocale(DEFAULT_LOCALE);
    });

    test("a reply with no usable locale leaves the current one alone", () => {
        useLocale("en_US");
        const luna = createFakeLuna();
        luna.answer(SYSTEM, () => ({ returnValue: true }));           // no locale field
        const watch = watchSystemLocale(luna);
        watch.start();
        assert.equal(currentLocale(), "en_US");                       // unchanged
        watch.stop();
        useLocale(DEFAULT_LOCALE);
    });

    test("stop cancels the subscription so a late push is ignored", () => {
        useLocale("en_US");
        const luna = createFakeLuna();
        luna.answer(SYSTEM, () => ({ returnValue: true, locale: { languageCode: "en", countryCode: "US" } }));
        const watch = watchSystemLocale(luna);
        watch.start();
        const sub = luna.subscribers.find((s) => s.uri === SYSTEM)!;
        watch.stop();
        sub.push({ returnValue: true, locale: { languageCode: "es", countryCode: "ES" } });
        assert.equal(currentLocale(), "en_US");                       // the late push did nothing
        useLocale(DEFAULT_LOCALE);
    });
});
