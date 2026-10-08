// The card's own life: what WebAppMgr says to the page, and what the card
// hears.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createPalmSystemApp } from "@webos/api/infra/app/palm-system.service.ts";
import { t, useTranslations } from "@webos/api/i18n/translate.ts";
import { DEFAULT_LOCALE, useLocale } from "@webos/api/i18n/locale.ts";

interface Hooks {
    stageActivated?: () => void;
    stageDeactivated?: () => void;
    relaunch?: () => void;
    keyboardShown?: (shown: boolean) => void;
    handleGesture?: (name: string, detail?: unknown) => void;
}

// A page, as WebAppMgr leaves it: PalmSystem for questions, window.Mojo for
// what it calls.
const fakePage = (system: Record<string, unknown> = {}, mojo: Record<string, unknown> = {}) => {
    const keys: ((event: { key: string }) => void)[] = [];
    const page = {
        removeEventListener: (_type: string, listener: (event: { key: string }) => void) => {
            const at = keys.indexOf(listener);
            if (at >= 0) {
                keys.splice(at, 1);
            }
        },
        PalmSystem: {
            launchParams: JSON.stringify({ target: "wifi" }),
            identifier: "com.gachlab.app.kit 1234",
            locale: "es_VE",
            stageReady: () => { page.ready = true; },
            addBannerMessage: (message: string) => {
                page.banners.push(message);
                return "1";
            },
            ...system,
        },
        Mojo: mojo,
        ready: false,
        closed: false,
        banners: [] as string[],
        close: () => { page.closed = true; },
        addEventListener: (_type: string, listener: (event: { key: string }) => void) => keys.push(listener),
        press: (key: string) => keys.forEach((listener) => listener({ key })),
    };
    return page;
};

describe("the card's life", () => {
    test("the launch parameters, the id and the locale come from PalmSystem", () => {
        const page = fakePage();
        const app = createPalmSystemApp({ window: page as never });
        assert.deepEqual(app.launchParams(), { target: "wifi" });
        assert.equal(app.identifier(), "com.gachlab.app.kit 1234");
        assert.equal(app.locale(), "es_VE");
    });

    test("parameters that are not JSON are no parameters, not a broken card", () => {
        const page = fakePage({ launchParams: "{not json" });
        assert.deepEqual(createPalmSystemApp({ window: page as never }).launchParams(), {});
        const none = fakePage({ launchParams: undefined });
        assert.deepEqual(createPalmSystemApp({ window: none as never }).launchParams(), {});
    });

    test("what WebAppMgr calls reaches whoever is listening", () => {
        const page = fakePage();
        let clock = 0;
        const app = createPalmSystemApp({ window: page as never, now: () => (clock += 1000) });
        const heard: string[] = [];
        app.on("activated", () => heard.push("activated"));
        app.on("deactivated", () => heard.push("deactivated"));
        app.on("keyboard", (shown) => heard.push(`keyboard:${shown}`));
        app.on("back", () => heard.push("back"));
        const stop = app.on("back", () => heard.push("twice"));

        const mojo = page.Mojo as Hooks;
        mojo.stageActivated?.();
        mojo.stageDeactivated?.();
        mojo.keyboardShown?.(true);
        mojo.handleGesture?.("back");
        stop();
        mojo.handleGesture?.("flick", { x: 1 });
        page.press("Escape");
        page.press("a");
        assert.deepEqual(heard, ["activated", "deactivated", "keyboard:true", "back", "twice", "back"]);
    });

    test("back is one back, however it arrives", () => {
        const page = fakePage();
        // The gesture and the key, in the same moment: a device with a
        // keyboard sends both, and a card that pops two screens for one
        // gesture is a card that cannot be navigated.
        const app = createPalmSystemApp({ window: page as never, now: () => 1000 });
        const heard: string[] = [];
        app.on("back", () => heard.push("back"));
        (page.Mojo as Hooks).handleGesture?.("back");
        page.press("Escape");
        assert.deepEqual(heard, ["back"]);
    });

    test("disposing lets go of the page", () => {
        const page = fakePage();
        const before = page.Mojo;
        const app = createPalmSystemApp({ window: page as never });
        const heard: string[] = [];
        app.on("activated", () => heard.push("activated"));
        const installed = page.Mojo as Hooks;
        app.dispose();
        installed.stageActivated?.();
        page.press("Escape");
        assert.deepEqual(heard, []);
        assert.equal(page.Mojo, before, "the hooks that were there are back");
    });

    test("the shell asks for the card's menu by relaunching it with palm-command", () => {
        const page = fakePage();
        const app = createPalmSystemApp({ window: page as never });
        const heard: string[] = [];
        app.on("menu", () => heard.push("menu"));
        app.on("relaunched", () => heard.push("relaunched"));
        (page.PalmSystem as { launchParams: string }).launchParams =
            JSON.stringify({ "palm-command": "open-app-menu" });
        (page.Mojo as Hooks).relaunch?.();
        assert.deepEqual(heard, ["menu"], "it is the menu, not a relaunch with parameters");
    });

    test("a relaunch carries the parameters it was relaunched with", () => {
        const page = fakePage();
        const app = createPalmSystemApp({ window: page as never });
        const seen: unknown[] = [];
        app.on("relaunched", (params) => seen.push(params));
        (page.PalmSystem as { launchParams: string }).launchParams = JSON.stringify({ target: "vpn" });
        (page.Mojo as Hooks).relaunch?.();
        assert.deepEqual(seen, [{ target: "vpn" }]);
    });

    test("hooks that were already there are kept, not replaced", () => {
        const heard: string[] = [];
        const page = fakePage({}, {
            stageActivated: () => heard.push("HP's own"),
            handleGesture: (name: string) => heard.push(`HP's own ${name}`),
        });
        const app = createPalmSystemApp({ window: page as never });
        app.on("activated", () => heard.push("ours"));
        const mojo = page.Mojo as Hooks;
        mojo.stageActivated?.();
        mojo.handleGesture?.("flick");
        assert.deepEqual(heard, ["HP's own", "ours", "HP's own flick"]);
    });

    test("ready, close and a banner go where they go", () => {
        const page = fakePage();
        const app = createPalmSystemApp({ window: page as never });
        app.ready();
        app.banner("Joined home");
        app.close();
        assert.equal(page.ready, true);
        assert.deepEqual(page.banners, ["Joined home"]);
        assert.equal(page.closed, true);
    });

    test("a page with no PalmSystem still answers, so a card opens in a browser", () => {
        const page = { Mojo: {}, addEventListener: () => {}, removeEventListener: () => {} };
        const app = createPalmSystemApp({ window: page as never });
        assert.deepEqual(app.launchParams(), {});
        assert.equal(app.locale(), "en_US");
        app.ready();
        app.banner("nothing happens");
    });
});

describe("what the user reads", () => {
    // t() reads the table for the locale now in force. The default is en_US,
    // which has no table, so the key -- English -- is what shows.
    test("the English text is the key, so a missing translation is still English", () => {
        useLocale("es_ES");
        useTranslations({});
        assert.equal(t("Turn on Wi-Fi"), "Turn on Wi-Fi");
        useTranslations({ es: { "Turn on Wi-Fi": "Encender el Wi-Fi" } });
        assert.equal(t("Turn on Wi-Fi"), "Encender el Wi-Fi");
        assert.equal(t("Not translated yet"), "Not translated yet");
        // Not through Object's own: t("constructor") is the word, not a function.
        assert.equal(t("constructor"), "constructor");
        assert.equal(t("toString"), "toString");
        useLocale(DEFAULT_LOCALE);
        useTranslations({});
    });

    test("English stays English even when a Spanish table is loaded", () => {
        useTranslations({ es: { "Turn on Wi-Fi": "Encender el Wi-Fi" } });
        useLocale("en_US");
        assert.equal(t("Turn on Wi-Fi"), "Turn on Wi-Fi");
        useLocale("es_ES");
        assert.equal(t("Turn on Wi-Fi"), "Encender el Wi-Fi");
        useLocale(DEFAULT_LOCALE);
        useTranslations({});
    });

    test("a region with no table of its own falls to the language's", () => {
        useTranslations({ es: { "Country": "País" } });
        useLocale("es_MX"); // no es_mx table; resolves through "es"
        assert.equal(t("Country"), "País");
        useLocale(DEFAULT_LOCALE);
        useTranslations({});
    });

    test("HP's placeholders are filled, and an unknown one is left alone", () => {
        useLocale("es_ES");
        useTranslations({ es: { "Joining #{name}...": "Uniendo a #{name}..." } });
        assert.equal(t("Joining #{name}...", { name: "home" }), "Uniendo a home...");
        assert.equal(t("#{a} and #{b}", { a: "one" }), "one and #{b}");
        useLocale(DEFAULT_LOCALE);
        useTranslations({});
    });
});


describe("a card follows the system locale", () => {
    // A minimal AppService: connectCard only needs locale(), launchParams(),
    // the lifecycle on()/ready()/close()/dispose() to connect a card. This
    // stands in for WebAppMgr's.
    const fakeApp = (locale: string) => ({
        launchParams: () => ({}),
        identifier: () => "test",
        locale: () => locale,
        ready: () => {},
        close: () => {},
        banner: () => {},
        on: () => () => {},
        dispose: () => {},
    });

    const nullService = () => {
        const state = { name: "x", data: {} };
        return {
            getState: () => state,
            // Like createState: a new subscriber hears the current state at
            // once, which is what drives the first paint.
            onStateChange: (listener: (s: typeof state) => void) => {
                listener(state);
                return () => {};
            },
        };
    };

    test("connectCard sets the kit's locale from app.locale(), before the first paint", async () => {
        const { connectCard } = await import("@webos/api/infra/app/connect-card.ts");
        const { currentLocale } = await import("@webos/api/i18n/locale.ts");

        let localeAtPaint = "";
        connectCard({
            service: nullService() as never,
            app: fakeApp("es_ES") as never,
            paint: () => { localeAtPaint = currentLocale(); },
        });
        // The locale was set from app.locale() verbatim -- and already in force
        // when the first frame was painted, not after.
        assert.equal(currentLocale(), "es_ES");
        assert.equal(localeAtPaint, "es_ES");
    });

    test("an app with no locale falls back rather than clearing it", async () => {
        const { connectCard } = await import("@webos/api/infra/app/connect-card.ts");
        const { currentLocale, DEFAULT_LOCALE } = await import("@webos/api/i18n/locale.ts");
        connectCard({
            service: nullService() as never,
            app: fakeApp("") as never,
        });
        assert.equal(currentLocale(), DEFAULT_LOCALE);
    });

    test("the card repaints when the language changes under it (hot reload)", async () => {
        const { connectCard } = await import("@webos/api/infra/app/connect-card.ts");
        const { useLocale, currentLocale } = await import("@webos/api/i18n/locale.ts");
        useLocale("en_US");

        const paints: string[] = [];
        connectCard({
            service: nullService() as never,
            app: fakeApp("en_US") as never,
            // Record the locale in force at each paint.
            paint: () => paints.push(currentLocale()),
        });
        const afterFirst = paints.length; // the first frame(s)

        // The language changes elsewhere (Regional Settings wrote it, the watch
        // called useLocale). The card must repaint, now in Spanish.
        useLocale("es_ES");
        assert.ok(paints.length > afterFirst, "a locale change repainted the card");
        assert.equal(paints[paints.length - 1], "es_ES");

        useLocale("en_US");
    });
});
