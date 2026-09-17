// The kit, on screen: every control the cards are built from, in each of the
// states it can be in.
//
// It is documentation that cannot go stale -- it is the kit itself -- and it is
// how a change to kit.css is looked at before it reaches a card. It talks to no
// service: its state machine is the showcase's own, written the way a card's
// is, so this file is also what a card looks like.

import { createState } from "#lib/helpers/create-state.ts";
import { t } from "#lib/i18n/translate.ts";
import { html } from "#ui/element.ts";
import { error, note } from "#ui/kit/kit.ts";
import { startCard, type CardService } from "#ui/start-card.ts";
import type { State } from "#lib/helpers/create-state.ts";

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
            const { dialog, choosing } = state.get().data;
            if (dialog || choosing) {
                state.patch({ dialog: false, choosing: false });
                return true;
            }
            return false;
        },
    };
};

const section = (title: string, body: unknown) => html`
    <div class="hp-group">
        <div class="hp-group-title">${title}</div>
        ${body}
    </div>`;

const view = (state: State<Shown>, service: ShowcaseService) => {
    const shown = state.data;
    return html`
    <div class="hp-card">
        <hp-header title="Kit" back></hp-header>
        <div class="hp-body">
            ${note("Every control, in each state. This card is the kit itself, so it cannot go stale.")}

            ${section("Buttons", html`
                <div class="hp-list">
                    <hp-row><hp-button label="Plain"
                                       @press=${() => service.change({ pressed: "Plain" })}></hp-button></hp-row>
                    <hp-row><hp-button label="Affirmative" kind="affirmative"
                                       @press=${() => service.change({ pressed: "Affirmative" })}></hp-button></hp-row>
                    <hp-row><hp-button label="Negative" kind="negative"
                                       @press=${() => service.change({ pressed: "Negative" })}></hp-button></hp-row>
                    <hp-row><hp-button label="Disabled" disabled></hp-button></hp-row>
                </div>
                ${shown.pressed ? note(`Last pressed: ${shown.pressed}`) : ""}`)}

            ${section("Toggles", html`
                <div class="hp-list">
                    <hp-row title="Answers with an event" detail="The card decides whether it becomes the new state">
                        <hp-toggle ?on=${shown.toggled}
                                   @toggle=${(e: CustomEvent<{ on: boolean }>) => service.change({ toggled: e.detail.on })}>
                        </hp-toggle>
                    </hp-row>
                    <hp-row title="With its own words">
                        <hp-toggle on label-on="Yes" label-off="No"></hp-toggle>
                    </hp-row>
                    <hp-row title="Disabled"><hp-toggle disabled></hp-toggle></hp-row>
                </div>`)}

            ${section("Rows", html`
                <div class="hp-list">
                    <hp-row title="One line" @select=${() => service.change({ pressed: "the first row" })}></hp-row>
                    <hp-row title="Two lines" detail="The second one is the detail"></hp-row>
                    <hp-row title="With something on the right" detail="Tapping the toggle does not select the row">
                        <hp-toggle on></hp-toggle>
                    </hp-row>
                    <hp-row title="A long title that has to be cut rather than pushed off the row"
                            detail="Ellipsis, not overflow"></hp-row>
                </div>`)}

            ${section("Fields and checks", html`
                <div class="hp-list">
                    <hp-field label="Network name" placeholder="Type here"
                              @change=${(e: CustomEvent<{ value: string }>) => service.change({ typed: e.detail.value })}>
                    </hp-field>
                    <hp-field label="Password" type="password" placeholder="Hidden while typing"></hp-field>
                    <hp-row title="A checkbox" detail=${shown.checked ? "Ticked" : "Not ticked"}>
                        <hp-check ?checked=${shown.checked}
                                  @change=${(e: CustomEvent<{ checked: boolean }>) => service.change({ checked: e.detail.checked })}>
                        </hp-check>
                    </hp-row>
                    <hp-row title="Disabled"><hp-check disabled></hp-check></hp-row>
                </div>
                ${shown.typed ? note(t("Typed: #{what}", { what: shown.typed })) : ""}`)}

            ${section("Choosing one of several", html`
                <div class="hp-list">
                    <hp-selector label="When to connect" value=${shown.when} .choices=${CHOICES}
                                 ?open=${shown.choosing}
                                 @open=${(e: CustomEvent<{ open: boolean }>) => service.change({ choosing: e.detail.open })}
                                 @choose=${(e: CustomEvent<{ value: string }>) =>
                                     service.change({ when: e.detail.value, choosing: false })}>
                    </hp-selector>
                </div>`)}

            ${section("Waiting and failing", html`
                <div class="hp-list">
                    <hp-spinner label="Always with words beside it"></hp-spinner>
                    <hp-progress value="40" label="Downloading"></hp-progress>
                </div>
                ${error("A failure says what the service said, never the uri.")}`)}

            ${section("Asking before doing", html`
                <div class="hp-list">
                    <hp-row title="A dialog" detail=${shown.answered || "Nothing chosen yet"}>
                        <hp-button label="Open" @press=${() => service.change({ dialog: true })}></hp-button>
                    </hp-row>
                </div>`)}

            ${section("What WebAppMgr says to the card", html`
                <div class="hp-list">
                    ${shown.life.length === 0
                        ? html`<hp-row title="Nothing yet" detail="Send the card away and bring it back"></hp-row>`
                        : shown.life.map((line) => html`<hp-row title=${line}></hp-row>`)}
                </div>`)}

            ${shown.dialog
                ? html`<hp-dialog title="Forget this network?"
                                  message="The password will have to be typed again."
                                  .buttons=${[
                                      { value: "forget", label: "Forget", kind: "negative" },
                                      { value: "keep", label: "Keep it" },
                                  ]}
                                  @choose=${(e: CustomEvent<{ value: string }>) =>
                                      service.change({ dialog: false, answered: `Chose: ${e.detail.value}` })}
                                  @dismiss=${() => service.change({ dialog: false, answered: "Dismissed" })}>
                       </hp-dialog>`
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
