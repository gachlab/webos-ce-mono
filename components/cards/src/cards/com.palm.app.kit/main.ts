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
    readonly swiped: boolean;
    readonly forgotten: string;
    readonly menu: boolean;
    readonly chose: string;
    readonly busy: boolean;
    readonly sleeps: string;
    readonly level: number;
    readonly dragging: number;
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
            level: 60, dragging: 0,
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

            ${section("Swipe to delete", html`
                <div class="hp-list">
                    <hp-swipe-row title="Swipe this one to the left" detail=${shown.forgotten || "Then confirm, as HP's lists do"}
                                  ?open=${shown.swiped}
                                  @open=${(e: CustomEvent<{ open: boolean }>) => service.change({ swiped: e.detail.open })}
                                  @remove=${() => service.change({ swiped: false, forgotten: "Deleted, and put back for the next swipe" })}>
                    </hp-swipe-row>
                    <hp-swipe-row title="This one deletes on the swipe itself" detail="No confirmation" instant
                                  @remove=${() => service.change({ forgotten: "Deleted without asking" })}>
                    </hp-swipe-row>
                </div>`)}

            ${section("One of a few", html`
                <div class="hp-list">
                    <hp-choice label="When device sleeps" value=${shown.sleeps}
                               .choices=${[{ value: "on", label: "Stay on" }, { value: "off", label: "Turn off" }]}
                               @choose=${(e: CustomEvent<{ value: string }>) => service.change({ sleeps: e.detail.value })}>
                    </hp-choice>
                </div>`)}

            ${section("A button that is working", html`
                <div class="hp-list">
                    <hp-row>
                        <hp-activity-button label=${shown.busy ? "Joining..." : "Join"} kind="affirmative"
                                            ?busy=${shown.busy}
                                            @press=${() => service.change({ busy: true })}></hp-activity-button>
                    </hp-row>
                </div>
                ${shown.busy ? note("Tap the card's menu to stop it.") : ""}`)}

            ${section("Sliders", html`
                <div class="hp-list">
                    <hp-row title="Brightness" detail=${`${shown.level}%`}>
                        <hp-slider value=${shown.level}
                                   @changing=${(e: CustomEvent<{ value: number }>) =>
                                       service.change({ level: e.detail.value, dragging: shown.dragging + 1 })}
                                   @change=${(e: CustomEvent<{ value: number }>) =>
                                       service.change({ level: e.detail.value })}>
                        </hp-slider>
                    </hp-row>
                    <hp-row title="Disabled"><hp-slider value="30" disabled></hp-slider></hp-row>
                </div>
                ${note(`While the finger is down it says "changing" (${shown.dragging} so far); when it lifts, "change" -- which is what a card writes to a service.`)}`)}

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

            <hp-app-menu ?open=${shown.menu}
                         .items=${[
                             { value: "stop", label: "Stop the busy button" },
                             { value: "help", label: "Help" },
                             { value: "nothing", label: "Disabled", disabled: true },
                         ]}
                         @close=${() => service.change({ menu: false })}
                         @choose=${(e: CustomEvent<{ value: string }>) =>
                             service.change({ menu: false, busy: false, chose: e.detail.value })}>
            </hp-app-menu>

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
app.on("menu", () => {
    service.heard("The app menu was asked for");
    service.change({ menu: true });
});
