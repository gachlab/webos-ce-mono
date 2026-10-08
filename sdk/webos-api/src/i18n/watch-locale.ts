// Follow the system's language while a card is open.
//
// Regional Settings writes the locale to com.palm.systemservice; that card's
// own process repaints itself, but every other live card is a separate page
// that only hears the change over the bus. This subscribes to the locale
// preference and calls useLocale when it moves, so t() and the date pickers in
// any card follow the device's language without the card being torn down and
// relaunched. connectCard repaints on the useLocale that results.
//
// It is the kit side of the hot reload (#19). A card opts in by handing it the
// bus; the subscription follows the card the way createWatch does, so a card
// sent away is not pushed at.

import type { LunaService, Payload } from "../infra/luna/service.ts";
import { createWatch, type Watch } from "../helpers/watch.ts";
import { useLocale } from "./locale.ts";

const SYSTEMSERVICE = "luna://com.palm.systemservice/getPreferences";

const text = (value: unknown): string => (typeof value === "string" ? value : "");

// The "xx_yy" tag from a {languageCode, countryCode} locale object, or "" if
// the reply has no usable locale. Lower-cased, as the translations table and
// the date-fields table are keyed. Exported so a test pins it without the bus.
export const localeTagOf = (reply: Payload): string => {
    const locale = reply.locale;
    if (!locale || typeof locale !== "object")
        return "";
    const obj = locale as Payload;
    const language = text(obj.languageCode).toLowerCase();
    const country = text(obj.countryCode).toLowerCase();
    if (!language || !country)
        return "";
    return `${language}_${country}`;
};

// Start following the system locale. Returns a Watch (start/stop/watching) so
// the caller can tie it to the card's life -- or call start() once and let
// connectCard's dispose cancel it. The subscription itself is what carries the
// change; onError is swallowed, because a transient preference-read failure is
// not a reason to stop following the locale (the subscription stays).
export const watchSystemLocale = (luna: LunaService): Watch =>
    createWatch(() =>
        luna.subscribe(
            SYSTEMSERVICE,
            { keys: ["locale"] },
            (reply: Payload) => {
                const tag = localeTagOf(reply);
                if (tag)
                    useLocale(tag);
            },
            () => { /* a transient read failure: keep following */ },
        ));
