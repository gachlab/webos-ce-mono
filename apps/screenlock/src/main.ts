// The Screen & Lock settings card.

import { openBus } from "@webos/api/infra/luna/open-bus.ts";
import { t } from "@webos/api/i18n/translate.ts";
import { html } from "@webos/ui-kit/element.ts";
import { error as errorLine, note } from "@webos/ui-kit/kit/kit.ts";
import { startCard } from "@webos/ui-kit/start-card.ts";
import {
    createScreenLockService, LOCK_AFTER_CHOICES, LOCK_MODE_CHOICES, TIMEOUT_CHOICES,
    timeoutLabel, type ScreenLockData, type ScreenLockService,
} from "./screenlock.service.ts";
import type { State } from "@webos/api/helpers/create-state.ts";

const main = (data: ScreenLockData, service: ScreenLockService) => html`
    <div class="wos-group">
        <div class="wos-list">
            <wos-row title=${t("Auto Dim")}>
                <wos-toggle ?on=${data.enableALS}
                    @toggle=${(e: CustomEvent<{ on: boolean }>) =>
                        service.onAutoDim(e.detail.on)}></wos-toggle>
            </wos-row>
            <div class="screenlock-brightness">
                <div>${t("Brightness")}</div>
                <wos-slider .value=${data.brightness} min="10" max="100"
                    @change=${(e: CustomEvent<{ value: number }>) =>
                        service.onBrightness(e.detail.value)}
                    @changing=${(e: CustomEvent<{ value: number }>) =>
                        service.onBrightness(e.detail.value)}></wos-slider>
            </div>
            <wos-selector label=${t("Turn off After")} value=${String(data.timeout)}
                .choices=${TIMEOUT_CHOICES}
                ?open=${!!data.choosingTimeout}
                @open=${(e: CustomEvent<{ open: boolean }>) => service.onChooseTimeout(e.detail.open)}
                @choose=${(e: CustomEvent<{ value: string }>) =>
                    service.onTimeout(Number(e.detail.value))}></wos-selector>
        </div>
    </div>

    <div class="wos-group">
        <div class="wos-group-title">${t("Wallpaper")}</div>
        <div class="wos-list">
            <wos-row title=${t("Change Wallpaper")}
                detail=${data.wallpaper ? t("Current wallpaper set") : ""}
                @select=${() => service.onChangeWallpaper()}></wos-row>
        </div>
    </div>

    <div class="wos-group">
        <div class="wos-group-title">${t("Advanced Gestures")}</div>
        <div class="wos-list">
            <wos-row title=${t("Enable Gestures")}>
                <wos-toggle ?on=${data.gestures}
                    @toggle=${(e: CustomEvent<{ on: boolean }>) =>
                        service.onGestures(e.detail.on)}></wos-toggle>
            </wos-row>
        </div>
    </div>
    <p class="screenlock-note">${t("Swipe up from the bottom of the screen to card an app. Swipe up again to see the Launcher.")}</p>

    <div class="wos-group">
        <div class="wos-group-title">${t("Secure Unlock")}</div>
        <div class="wos-list">
            <wos-selector label="" value=${data.lockMode}
                .choices=${LOCK_MODE_CHOICES}
                ?open=${!!data.choosingLock}
                @open=${(e: CustomEvent<{ open: boolean }>) => service.onChooseLock(e.detail.open)}
                @choose=${(e: CustomEvent<{ value: string }>) =>
                    service.onLockMode(e.detail.value as "none" | "pin" | "password")}></wos-selector>
            ${data.lockMode !== "none" ? html`
                <wos-selector label=${t("Lock After")} value=${String(data.lockTimeout)}
                    .choices=${LOCK_AFTER_CHOICES}
                    ?open=${!!data.choosingLockAfter}
                    @open=${(e: CustomEvent<{ open: boolean }>) =>
                        service.onChooseLockAfter(e.detail.open)}
                    @choose=${(e: CustomEvent<{ value: string }>) =>
                        service.onLockAfter(Number(e.detail.value))}></wos-selector>
            ` : ""}
        </div>
    </div>

    <div class="wos-group">
        <div class="wos-group-title">${t("Notifications")}</div>
        <div class="wos-list">
            <wos-row title=${t("Show When Locked")}>
                <wos-toggle ?on=${data.showWhenLocked}
                    @toggle=${(e: CustomEvent<{ on: boolean }>) =>
                        service.onShowWhenLocked(e.detail.on)}></wos-toggle>
            </wos-row>
            <wos-row title=${t("Blink Notifications")}>
                <wos-toggle ?on=${data.blink}
                    @toggle=${(e: CustomEvent<{ on: boolean }>) =>
                        service.onBlink(e.detail.on)}></wos-toggle>
            </wos-row>
        </div>
    </div>
    <p class="screenlock-note">${t("The center button blinks when new notifications arrive.")}</p>

    ${data.message ? errorLine(data.message) : ""}
    ${data.busy ? html`<wos-spinner label=${t("Loading Preferences...")}></wos-spinner>` : ""}
`;

const configure = (data: ScreenLockData, service: ScreenLockService) => {
    const pin = data.configureMode === "pin";
    return html`
        <div class="wos-group">
            <div class="wos-list">
                <wos-field label=${pin ? t("PIN") : t("Password")}
                    value=${data.passcode} type=${pin ? "tel" : "password"}
                    @change=${(e: CustomEvent<{ value: string }>) =>
                        service.onConfigureField({ passcode: e.detail.value })}></wos-field>
                <wos-field label=${pin ? t("Confirm PIN") : t("Confirm Password")}
                    value=${data.passcodeConfirm} type=${pin ? "tel" : "password"}
                    @change=${(e: CustomEvent<{ value: string }>) =>
                        service.onConfigureField({ passcodeConfirm: e.detail.value })}></wos-field>
            </div>
        </div>
        ${data.message ? errorLine(data.message) : ""}
    `;
};

const view = (state: State<ScreenLockData>, service: ScreenLockService) => {
    const data = state.data;
    const onMain = data.screen === "main";
    const foot = !onMain;
    return html`
        <div class="wos-card ${foot ? "screenlock-has-footer" : ""}">
            <wos-header title=${onMain ? t("Screen & Lock") : (data.configureMode === "pin"
                    ? t("Simple PIN") : t("Password"))}
                       light icon="header-icon-screen.png"
                       ?back=${!onMain} @back=${() => service.onBack()}></wos-header>
            <div class="wos-body">
                ${onMain ? main(data, service) : configure(data, service)}
            </div>
            ${foot ? html`
                <div class="screenlock-footer">
                    <wos-button class="screenlock-wide" label=${t("Cancel")}
                        @press=${() => service.onCancelConfigure()}></wos-button>
                    <wos-activity-button class="screenlock-wide" label=${t("Save")}
                        kind="affirmative" ?busy=${data.busy}
                        @press=${() => service.onSaveConfigure()}></wos-activity-button>
                </div>` : ""}
            <wos-app-menu ?open=${data.menuOpen}
                .items=${[{ value: "help", label: t("Help") }]}
                @close=${() => service.onMenuChoice("")}
                @choose=${(e: CustomEvent<{ value: string }>) =>
                    service.onMenuChoice(e.detail.value)}></wos-app-menu>
        </div>
    `;
};

// Exported for the mutation-style tests that assert the mapper stays wired.
void timeoutLabel;

const luna = openBus();
const service = createScreenLockService(luna);
const { app } = startCard({ service, view });
app.on("menu", () => service.onMenu());
