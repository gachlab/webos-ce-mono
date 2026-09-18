// The Wi-Fi settings card.
//
// Five screens, all of them a function of the state in
// lib/services/wifi.service.ts: the network list, the join screen, the address
// settings of the joined network, the known networks, and what the radio does
// while the machine sleeps. Nothing here decides anything; it draws what the
// service says and tells it what the user did.

import { openBus } from "@webos/ui-kit/open-bus.ts";
import { t } from "@webos/api/i18n/translate.ts";
import { html } from "@webos/ui-kit/element.ts";
import { error as errorLine, note } from "@webos/ui-kit/kit/kit.ts";
import { startCard } from "@webos/ui-kit/start-card.ts";
import { canJoin, canSaveAddress, createWifiService,
         type AddressFields, type JoinFields, type WifiData, type WifiService } from "./wifi.service.ts";
import type { Network, Security } from "./luna/wifi.ts";
import type { State } from "@webos/api/helpers/create-state.ts";

const SECURITY: { value: Security; label: string }[] = [
    { value: "none", label: t("Open") },
    { value: "wpa-personal", label: t("WPA Personal") },
    { value: "wep", label: t("WEP") },
    { value: "enterprise", label: t("Enterprise") },
];

const securityLabel = (security: Security): string =>
    SECURITY.find((one) => one.value === security)?.label ?? "";

// What a row says under the name, which is what the network is doing.
const networkStatus = (network: Network): string => {
    switch (network.connectState) {
    case "associating":
    case "associated":
        return t("CONNECTING...");
    case "ipFailed":
        return t("TAP TO CONFIGURE IP ADDRESS");
    case "associationFailed":
        return network.lastError === "IncorrectPasskey" || network.lastError === "IncorrectPassword"
            ? t("INCORRECT PASSWORD")
            : t("ASSOCIATION FAILED");
    default:
        return "";
    }
};

// HP's signal: a dot with two arcs over it, as many lit as the signalBars the
// service sends (1 to 3). Drawn rather than shipped: these are the shapes and
// the sizes of wifi-icon-excellent.png and its brothers, read off the images
// themselves, in a picture that scales with the text instead of staying the
// size somebody exported it at.
const bars = (network: Network) => html`
    <svg class="wifi-signal" viewBox="0 0 33 25" role="img"
         aria-label=${t("Signal #{bars} of 3", { bars: String(network.bars) })}>
        <circle class=${network.bars >= 1 ? "lit" : ""} cx="16.5" cy="20.5" r="2"></circle>
        <path class=${network.bars >= 2 ? "lit" : ""} d="M9.5 15.6A8.5 8.5 0 0 1 23.5 15.6"></path>
        <path class=${network.bars >= 3 ? "lit" : ""} d="M3.4 11.3A16 16 0 0 1 29.6 11.3"></path>
    </svg>`;

// secure-icon.png: the shackle over a body that fills the width.
const padlock = () => html`
    <svg class="wifi-lock" viewBox="0 0 14 25" role="img" aria-label=${t("Secured")}>
        <path d="M2.5 15V9.5a4 4 0 0 1 8 0V15"></path>
        <rect x="0" y="14" width="14" height="11" rx="1"></rect>
    </svg>`;

// checkmark.png, which is the only blue thing in the list.
const tick = () => html`
    <svg class="wifi-joined" viewBox="0 0 32 25" role="img" aria-label=${t("Connected")}>
        <path d="M8 13.5 14 21 26 4.5"></path>
    </svg>`;

// join-plus-icon.png, at the left of the row that opens the join screen.
const plus = () => html`
    <svg class="wifi-plus" slot="lead" viewBox="0 0 18 18" aria-hidden="true">
        <path d="M7 0h4v7h7v4h-7v7h-4v-7H0V7h7z"></path>
    </svg>`;

const list = (data: WifiData, service: WifiService) => html`
    ${data.radio
        ? html`
            <div class="wos-group">
                <div class="wos-group-title">${t("Choose a network")}</div>
                <div class="wos-list">
                    <div class="wifi-networks">
                    ${data.scanning && data.networks.length === 0
                        ? html`<wos-spinner label=${t("Searching for networks...")}></wos-spinner>`
                        : ""}
                    ${data.networks.map((network) => html`
                        <wos-row title=${network.ssid}
                                detail=${networkStatus(network)}
                                ?strong=${network.connectState === "ipConfigured"
                                          || network.connectState === "associated"}
                                @select=${() => service.onNetwork(network.ssid)}>
                            <span class="wifi-marks">
                                ${network.connectState === "ipConfigured" ? tick() : ""}
                                ${network.security !== "none" ? padlock() : ""}
                                ${bars(network)}
                            </span>
                        </wos-row>`)}
                    </div>
                    <wos-row class="wifi-join" title=${t("Join Network")}
                            @select=${() => service.onJoinOther()}>${plus()}</wos-row>
                </div>
            </div>
            ${note(t("Your device automatically connects to known networks."))}`
        : html`<div class="wos-group"><p class="wifi-off">${t("Wi-Fi is turned off.")}</p></div>`}`;

const join = (fields: JoinFields, data: WifiData, service: WifiService) => html`
    <div class="wos-group">
        <div class="wos-list">
            ${fields.fixed
                ? ""
                : html`
                    <wos-field label=${t("NETWORK NAME")} value=${fields.ssid} placeholder=${t("Enter network name")}
                              @change=${(e: CustomEvent<{ value: string }>) =>
                                  service.onJoinField({ ssid: e.detail.value })}></wos-field>
                    <wos-choice label=${t("NETWORK SECURITY")} value=${fields.security} .choices=${SECURITY}
                               @choose=${(e: CustomEvent<{ value: string }>) =>
                                   service.onJoinField({ security: e.detail.value as Security })}></wos-choice>`}
            ${fields.security === "enterprise"
                ? html`<wos-field label=${t("Username")} value=${fields.userName}
                                 @change=${(e: CustomEvent<{ value: string }>) =>
                                     service.onJoinField({ userName: e.detail.value })}></wos-field>`
                : ""}
            ${fields.security !== "none"
                ? html`<wos-field label=${t("Password")} type="password" value=${fields.password}
                                 @change=${(e: CustomEvent<{ value: string }>) =>
                                     service.onJoinField({ password: e.detail.value })}
                                 @done=${() => service.onJoin()}></wos-field>`
                : ""}
        </div>
    </div>
    ${data.joinMessage ? errorLine(data.joinMessage) : ""}
    <div class="wos-group">
        <wos-activity-button label=${data.joining ? t("Signing In...") : t("Sign In")} kind="dark"
                            ?busy=${data.joining} ?disabled=${!canJoin(fields)}
                            @press=${() => service.onJoin()}></wos-activity-button>
    </div>
    <div class="wos-group">
        <wos-button label=${t("Cancel")} @press=${() => service.onCancelJoin()}></wos-button>
    </div>`;

const address = (fields: AddressFields, data: WifiData, service: WifiService) => {
    const field = (label: string, key: keyof AddressFields, placeholder: string) => html`
        <wos-field label=${label} value=${String(fields[key])} placeholder=${placeholder}
                  ?disabled=${fields.automatic}
                  @change=${(e: CustomEvent<{ value: string }>) =>
                      service.onAddressField({ [key]: e.detail.value } as Partial<AddressFields>)}></wos-field>`;
    return html`
        <div class="wos-group">
            <div class="wos-list">
                <wos-row title=${t("Automatic IP settings")}>
                    <wos-toggle ?on=${fields.automatic}
                               @toggle=${(e: CustomEvent<{ on: boolean }>) =>
                                   service.onAddressField({ automatic: e.detail.on })}></wos-toggle>
                </wos-row>
            </div>
        </div>
        <div class="wos-group">
            <div class="wos-list">
                ${field(t("ADDRESS"), "ip", t("Enter IP address"))}
                ${field(t("SUBNET"), "subnet", t("Enter subnet mask"))}
                ${field(t("GATEWAY"), "gateway", t("Enter gateway address"))}
                ${field(t("DNS SERVER"), "dns1", t("Enter primary DNS server"))}
                ${field(t("DNS SERVER"), "dns2", t("Enter secondary DNS server (optional)"))}
            </div>
        </div>
        <div class="wos-group">
            <wos-button label=${t("Forget Network")} kind="negative"
                       @press=${() => service.onForget()}></wos-button>
        </div>
        <div class="wos-group">
            <wos-activity-button label=${t("Done")} ?busy=${data.addressBusy}
                                ?disabled=${!canSaveAddress(fields)}
                                @press=${() => service.onSaveAddress()}></wos-activity-button>
        </div>`;
};

const known = (data: WifiData, service: WifiService) => html`
    ${data.knownUnreadable
        ? note(t("No known networks."))
        : html`
            <div class="wos-group">
                <div class="wos-group-title">${t("Known Networks")}</div>
                <div class="wos-list">
                    ${(data.known ?? []).map((profile) => html`
                        <wos-swipe-row title=${profile.ssid} detail=${securityLabel(profile.security)}
                                      confirm=${t("Delete")}
                                      @remove=${() => service.onForgetKnown(profile.profileId)}>
                        </wos-swipe-row>`)}
                </div>
            </div>`}`;

const settings = (data: WifiData, service: WifiService) => html`
    <div class="wos-group">
        <div class="wos-group-title">${t("When Device Sleeps")}</div>
        <div class="wos-list">
            <wos-choice value=${data.sleep}
                       .choices=${[{ value: "enable", label: t("Keep Wi-Fi On") },
                                   { value: "disable", label: t("Turn Wi-Fi Off") }]}
                       @choose=${(e: CustomEvent<{ value: string }>) => service.onSleep(e.detail.value)}>
            </wos-choice>
        </div>
    </div>
    ${note(data.sleep === "disable"
        ? t("May provide better battery life when connected to some Wi-Fi networks.")
        : t("Best for prolonging battery life in most cases."))}`;

const view = (state: State<WifiData>, service: WifiService) => {
    const data = state.data;
    const onList = data.screen === "list";
    return html`
        <div class="wos-card">
            <wos-header title=${t("Wi-Fi")} light icon="header-icon-wifi.png"
                       ?back=${!onList} @back=${() => service.onBack()}>
                ${onList
                    ? html`<wos-toggle ?on=${data.radioWanted ?? data.radio}
                                      ?disabled=${data.radioWanted !== undefined}
                                      @toggle=${(e: CustomEvent<{ on: boolean }>) => service.onRadio(e.detail.on)}>
                           </wos-toggle>`
                    : ""}
            </wos-header>
            <div class="wos-body">
                ${data.caption ? html`<p class="wifi-caption">${data.caption}</p>` : ""}
                ${data.screen === "list" ? list(data, service) : ""}
                ${data.screen === "join" && data.join ? join(data.join, data, service) : ""}
                ${data.screen === "address" && data.address ? address(data.address, data, service) : ""}
                ${data.screen === "known" ? known(data, service) : ""}
                ${data.screen === "settings" ? settings(data, service) : ""}
            </div>
            <wos-app-menu ?open=${service.menuOpen()}
                         .items=${[
                             { value: "settings", label: t("Settings") },
                             { value: "known", label: t("Known Networks") },
                             { value: "help", label: t("Help") },
                         ]}
                         @close=${() => service.onMenuChoice("")}
                         @choose=${(e: CustomEvent<{ value: string }>) => {
                             if (e.detail.value === "help") {
                                 openHelp();
                             }
                             service.onMenuChoice(e.detail.value);
                         }}>
            </wos-app-menu>
        </div>`;
};

const luna = openBus();

// help.palm.com is gone; webOS Archive's copy has no Wi-Fi page of its own, so
// this opens its English index, as the card before this one did.
const openHelp = () => {
    void luna.call("luna://com.palm.applicationManager/open",
                   { target: "https://help.webosarchive.org/en-us/" })
        .catch((error: unknown) => console.warn(String(error)));
};

const service = createWifiService({
    luna,
    setInterval: (callback, ms) => setInterval(callback, ms),
    clearInterval: (handle) => clearInterval(handle as ReturnType<typeof setInterval>),
    log: (message) => console.warn(message),
});

const { app } = startCard({ service, view });
app.on("menu", () => service.onMenu());
