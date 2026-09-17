// The card's own life: what WebAppMgr says to the page, and what the card
// hears.

import assert from "node:assert/strict";
import { describe, test } from "node:test";

import { createPalmSystemApp } from "#lib/infra/app/palm-system.service.ts";
import { t, useTranslations } from "#lib/i18n/translate.ts";

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
        PalmSystem: {
            launchParams: JSON.stringify({ target: "wifi" }),
            identifier: "com.palm.app.kit 1234",
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
        assert.equal(app.identifier(), "com.palm.app.kit 1234");
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
        const app = createPalmSystemApp({ window: page as never });
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
        const page = { Mojo: {}, addEventListener: () => {} };
        const app = createPalmSystemApp({ window: page as never });
        assert.deepEqual(app.launchParams(), {});
        assert.equal(app.locale(), "en_US");
        app.ready();
        app.banner("nothing happens");
    });
});

describe("what the user reads", () => {
    test("the English text is the key, so a missing translation is still English", () => {
        useTranslations({});
        assert.equal(t("Turn on Wi-Fi"), "Turn on Wi-Fi");
        useTranslations({ "Turn on Wi-Fi": "Encender el Wi-Fi" });
        assert.equal(t("Turn on Wi-Fi"), "Encender el Wi-Fi");
        assert.equal(t("Not translated yet"), "Not translated yet");
    });

    test("HP's placeholders are filled, and an unknown one is left alone", () => {
        useTranslations({ "Joining #{name}...": "Uniendo a #{name}..." });
        assert.equal(t("Joining #{name}...", { name: "home" }), "Uniendo a home...");
        assert.equal(t("#{a} and #{b}", { a: "one" }), "one and #{b}");
        useTranslations({});
    });
});
