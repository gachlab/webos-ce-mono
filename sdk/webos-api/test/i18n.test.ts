// The date/time half of #19: the locale a card sets decides the pickers' month
// names, field order and clock. The pickers themselves draw into a shadow root
// (a DOM the test runner has not got), so what is tested here is the data they
// read -- dateFieldsFor for the pure lookup, and useLocale/dateFields for the
// ambient one a picker reads at render.

import assert from "node:assert/strict";
import { afterEach, describe, test } from "node:test";

import { currentLocale, DEFAULT_LOCALE, useLocale } from "@webos/api/i18n/locale.ts";
import { dateFields, dateFieldsFor } from "@webos/api/i18n/date-fields.ts";

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
