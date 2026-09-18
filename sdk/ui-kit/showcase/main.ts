// The kit, on screen: every control the cards are built from, in each of the
// states it can be in.
//
// It is documentation that cannot go stale -- it is the kit itself -- and it is
// how a change to kit.css is looked at before it reaches a card. It talks to no
// service: its state machine is the showcase's own, written the way a card's
// is, so this file is also what a card looks like.

import { createState } from "@webos/api/helpers/create-state.ts";
import { t } from "@webos/api/i18n/translate.ts";
import { html } from "@webos/ui-kit/element.ts";
import { error, note } from "@webos/ui-kit/kit/kit.ts";
import { startCard, type CardService } from "@webos/ui-kit/start-card.ts";
import type { State } from "@webos/api/helpers/create-state.ts";

interface Shown {
    readonly toggled: boolean;
    readonly pressed: string;
    readonly checked: boolean;
    readonly typed: string;
    readonly when: string;
    readonly choosing: boolean;
    readonly dialog: boolean;
    readonly answered: string;
    readonly life: string[];
    readonly swiped: boolean;
    readonly forgotten: string;
    readonly menu: boolean;
    readonly chose: string;
    readonly busy: boolean;
    readonly sleeps: string;
    readonly level: number;
    readonly dragging: number;
    readonly theme: string;
}

interface ShowcaseService extends CardService<Shown> {
    change(what: Partial<Shown>): void;
    heard(what: string): void;
}

const CHOICES = [
    { value: "off", label: "Never" },
    { value: "ask", label: "Always ask" },
    { value: "auto", label: "Automatically" },
];

const createShowcase = (): ShowcaseService => {
    const state = createState<Shown>({
        name: "kit:ready",
        data: {
            toggled: true, pressed: "", checked: true, typed: "", when: "ask",
            choosing: false, dialog: false, answered: "", life: [],
            swiped: false, forgotten: "", menu: false, chose: "", busy: false, sleeps: "off",
            level: 60, dragging: 0, theme: "enyo",
        },
    });
    return {
        getState: state.get,
        onStateChange: state.subscribe,
        change: (what) => state.patch(what),
        heard: (what) => state.patch({ life: [...state.get().data.life, what].slice(-6) }),
        onShown: () => state.patch({}),
        onBack: () => {
            // A card that has something open closes that first, and only then
            // lets the back gesture close the card.
            const { dialog, choosing, menu, swiped } = state.get().data;
            if (dialog || choosing || menu || swiped) {
                state.patch({ dialog: false, choosing: false, menu: false, swiped: false });
                return true;
            }
            return false;
        },
    };
};

// The two palettes are two stylesheets with the same names in them, so the
// showcase changes how everything looks by pointing one link somewhere else.
// It is here rather than in the kit because it is a thing to look at, not a
// thing a card does: a card links the theme it wants and never thinks again.
const THEMES = ["enyo", "modern"] as const;

const wearTheme = (name: string): void => {
    const link = document.querySelector<HTMLLinkElement>("link[href^=\"theme-\"]");
    if (link) {
        link.href = `theme-${name}.css`;
    }
};

const section = (title: string, body: unknown) => html`
    <div class="wos-group">
        <div class="wos-group-title">${title}</div>
        ${body}
    </div>`;

const view = (state: State<Shown>, service: ShowcaseService) => {
    const shown = state.data;
    return html`
    <div class="wos-card">
        <wos-header title="Kit" back></wos-header>
        <div class="wos-body">
            ${note("Every control, in each state. This card is the kit itself, so it cannot go stale.")}

            ${section("Theme", html`
                <div class="wos-list">
                    <wos-choice value=${shown.theme}
                               .choices=${THEMES.map((name) => ({ value: name, label: name === "enyo" ? "enyo" : "Modern" }))}
                               @choose=${(e: CustomEvent<{ value: string }>) => {
                                   wearTheme(e.detail.value);
                                   service.change({ theme: e.detail.value });
                               }}>
                    </wos-choice>
                </div>
                ${note(shown.theme === "enyo"
                    ? "HP's own values, read out of enyo's CSS and out of the images its theme draws with."
                    : "The same kit with a palette chosen for a screen somebody is looking at today.")}`)}

            ${section("Buttons", html`
                <div class="wos-list">
                    ${["Plain", "Dark", "Affirmative", "Negative", "Blue", "Gray"].map((name) => html`
                        <wos-row><wos-button label=${name} kind=${name === "Plain" ? "" : name.toLowerCase()}
                                           @press=${() => service.change({ pressed: name })}></wos-button></wos-row>`)}
                    <wos-row><wos-button label="Disabled" disabled></wos-button></wos-row>
                </div>
                ${note("The kinds are enyo's own, and so are their colours: plain is the light "
                       + "button its cards use for Cancel and Done, dark the one Wi-Fi signs in with.")}
                ${shown.pressed ? note(`Last pressed: ${shown.pressed}`) : ""}`)}

            ${section("Toggles", html`
                <div class="wos-list">
                    <wos-row title="Answers with an event" detail="The card decides whether it becomes the new state">
                        <wos-toggle ?on=${shown.toggled}
                                   @toggle=${(e: CustomEvent<{ on: boolean }>) => service.change({ toggled: e.detail.on })}>
                        </wos-toggle>
                    </wos-row>
                    <wos-row title="With its own words">
                        <wos-toggle on label-on="Yes" label-off="No"></wos-toggle>
                    </wos-row>
                    <wos-row title="Disabled"><wos-toggle disabled></wos-toggle></wos-row>
                </div>`)}

            ${section("Rows", html`
                <div class="wos-list">
                    <wos-row title="One line" @select=${() => service.change({ pressed: "the first row" })}></wos-row>
                    <wos-row title="Two lines" detail="The second one is the detail"></wos-row>
                    <wos-row title="With something on the right" detail="Tapping the toggle does not select the row">
                        <wos-toggle on></wos-toggle>
                    </wos-row>
                    <wos-row title="A long title that has to be cut rather than pushed off the row"
                            detail="Ellipsis, not overflow"></wos-row>
                </div>`)}

            ${section("Fields and checks", html`
                <div class="wos-list">
                    <wos-field label="Network name" placeholder="Type here"
                              @change=${(e: CustomEvent<{ value: string }>) => service.change({ typed: e.detail.value })}>
                    </wos-field>
                    <wos-field label="Password" type="password" placeholder="Hidden while typing"></wos-field>
                    <wos-row title="A checkbox" detail=${shown.checked ? "Ticked" : "Not ticked"}>
                        <wos-check ?checked=${shown.checked}
                                  @change=${(e: CustomEvent<{ checked: boolean }>) => service.change({ checked: e.detail.checked })}>
                        </wos-check>
                    </wos-row>
                    <wos-row title="Disabled"><wos-check disabled></wos-check></wos-row>
                    <wos-row title="Details without selecting">
                        <wos-info @press=${() => service.change({ typed: "info" })}></wos-info>
                    </wos-row>
                </div>
                ${shown.typed ? note(t("Typed: #{what}", { what: shown.typed })) : ""}`)}

            ${section("Choosing one of several", html`
                <div class="wos-list">
                    <wos-selector label="When to connect" value=${shown.when} .choices=${CHOICES}
                                 ?open=${shown.choosing}
                                 @open=${(e: CustomEvent<{ open: boolean }>) => service.change({ choosing: e.detail.open })}
                                 @choose=${(e: CustomEvent<{ value: string }>) =>
                                     service.change({ when: e.detail.value, choosing: false })}>
                    </wos-selector>
                </div>`)}

            ${section("Swipe to delete", html`
                <div class="wos-list">
                    <wos-swipe-row title="Swipe this one to the left" detail=${shown.forgotten || "Then confirm, as HP's lists do"}
                                  ?open=${shown.swiped}
                                  @open=${(e: CustomEvent<{ open: boolean }>) => service.change({ swiped: e.detail.open })}
                                  @remove=${() => service.change({ swiped: false, forgotten: "Deleted, and put back for the next swipe" })}>
                    </wos-swipe-row>
                    <wos-swipe-row title="This one deletes on the swipe itself" detail="No confirmation" instant
                                  @remove=${() => service.change({ forgotten: "Deleted without asking" })}>
                    </wos-swipe-row>
                </div>`)}

            ${section("One of a few", html`
                <div class="wos-list">
                    <!-- Without a label it takes the whole row, which is the
                         shape enyo's RadioGroup had. -->
                    <wos-choice value=${shown.sleeps}
                               .choices=${[{ value: "on", label: "Stay on" }, { value: "off", label: "Turn off" }]}
                               @choose=${(e: CustomEvent<{ value: string }>) => service.change({ sleeps: e.detail.value })}>
                    </wos-choice>
                    <wos-choice label="When device sleeps" value=${shown.sleeps}
                               .choices=${[{ value: "on", label: "Stay on" }, { value: "off", label: "Turn off" }]}
                               @choose=${(e: CustomEvent<{ value: string }>) => service.change({ sleeps: e.detail.value })}>
                    </wos-choice>
                </div>`)}

            ${section("A button that is working", html`
                <div class="wos-list">
                    <wos-row>
                        <wos-activity-button label=${shown.busy ? "Joining..." : "Join"} kind="dark"
                                            ?busy=${shown.busy}
                                            @press=${() => service.change({ busy: true })}></wos-activity-button>
                    </wos-row>
                    <!-- The same one in the light kind: the spinner turns in
                         whatever the button writes in, so it has to be legible
                         on both. -->
                    <wos-row>
                        <wos-activity-button label=${shown.busy ? "Saving..." : "Save"}
                                            ?busy=${shown.busy}
                                            @press=${() => service.change({ busy: true })}></wos-activity-button>
                    </wos-row>
                </div>
                ${shown.busy ? note("Tap the card's menu to stop it.") : ""}`)}

            ${section("Sliders", html`
                <div class="wos-list">
                    <wos-row title="Brightness" detail=${`${shown.level}%`}>
                        <wos-slider value=${shown.level}
                                   @changing=${(e: CustomEvent<{ value: number }>) =>
                                       service.change({ level: e.detail.value, dragging: shown.dragging + 1 })}
                                   @change=${(e: CustomEvent<{ value: number }>) =>
                                       service.change({ level: e.detail.value })}>
                        </wos-slider>
                    </wos-row>
                    <wos-row title="Disabled"><wos-slider value="30" disabled></wos-slider></wos-row>
                </div>
                ${note(`While the finger is down it says "changing" (${shown.dragging} so far); when it lifts, "change" -- which is what a card writes to a service.`)}`)}

            ${section("Waiting and failing", html`
                <div class="wos-list">
                    <wos-spinner label="Always with words beside it"></wos-spinner>
                    <wos-progress value="40" label="Downloading"></wos-progress>
                </div>
                ${error("A failure says what the service said, never the uri.")}`)}

            ${section("Asking before doing", html`
                <div class="wos-list">
                    <wos-row title="A dialog" detail=${shown.answered || "Nothing chosen yet"}>
                        <wos-button label="Open" @press=${() => service.change({ dialog: true })}></wos-button>
                    </wos-row>
                </div>`)}

            ${section("What WebAppMgr says to the card", html`
                <div class="wos-list">
                    ${shown.life.length === 0
                        ? html`<wos-row title="Nothing yet" detail="Send the card away and bring it back"></wos-row>`
                        : shown.life.map((line) => html`<wos-row title=${line}></wos-row>`)}
                </div>`)}

            <wos-app-menu ?open=${shown.menu}
                         .items=${[
                             { value: "stop", label: "Stop the busy button" },
                             { value: "help", label: "Help" },
                             { value: "nothing", label: "Disabled", disabled: true },
                         ]}
                         @close=${() => service.change({ menu: false })}
                         @choose=${(e: CustomEvent<{ value: string }>) =>
                             service.change({ menu: false, busy: false, chose: e.detail.value })}>
            </wos-app-menu>

            ${shown.dialog
                ? html`<wos-dialog title="Forget this network?"
                                  message="The password will have to be typed again."
                                  .buttons=${[
                                      { value: "forget", label: "Forget", kind: "negative" },
                                      { value: "keep", label: "Keep it" },
                                  ]}
                                  @choose=${(e: CustomEvent<{ value: string }>) =>
                                      service.change({ dialog: false, answered: `Chose: ${e.detail.value}` })}
                                  @dismiss=${() => service.change({ dialog: false, answered: "Dismissed" })}>
                       </wos-dialog>`
                : ""}
        </div>
    </div>`;
};

const service = createShowcase();
const { app } = startCard({ service, view });

// The other half of the showcase: what WebAppMgr says to a card, listed as it
// arrives. startCard already gives the service what a card usually needs --
// shown, hidden, back -- and hands over the rest.
app.on("activated", () => service.heard("Brought to the front"));
app.on("deactivated", () => service.heard("Sent away"));
app.on("relaunched", (params) => service.heard(`Relaunched with ${JSON.stringify(params)}`));
app.on("keyboard", (up) => service.heard(up ? "Keyboard came up" : "Keyboard went away"));
app.on("back", () => service.heard("Back"));
app.on("menu", () => {
    service.heard("The app menu was asked for");
    service.change({ menu: true });
});
