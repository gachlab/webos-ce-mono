// The kit, on screen: every control the cards are built from, in each of the
// states it can be in.
//
// It is documentation that cannot go stale -- it is the kit itself -- and it is
// how a change to hp.css is looked at before it reaches a card. It talks to
// nothing: no bus, no service, no state beyond what the controls are showing.

import styles from "#ui/hp.css";
import { createPalmSystemApp } from "#lib/infra/app/palm-system.service.ts";
import { t } from "#lib/i18n/translate.ts";
import { html, render, setProperties, useStyles } from "#ui/element.ts";
import { error, note } from "#ui/kit/kit.ts";

interface Shown {
    readonly toggled: boolean;
    readonly pressed: string;
    readonly checked: boolean;
    readonly typed: string;
    readonly speed: string;
    readonly dialog: boolean;
    readonly answered: string;
    readonly life: string[];
}

const section = (title: string, body: unknown) => html`
    <div class="hp-group">
        <div class="hp-group-title">${title}</div>
        ${body}
    </div>`;

const CHOICES = [
    { value: "off", label: "Never" },
    { value: "ask", label: "Always ask" },
    { value: "auto", label: "Automatically" },
];

// A property, not an attribute: a list cannot travel as text.
const selector = (shown: Shown, act: (change: Partial<Shown>) => void) => {
    const element = document.createElement("hp-selector");
    element.setAttribute("label", "When to connect");
    element.setAttribute("value", shown.speed);
    element.addEventListener("choose", ((e: CustomEvent<{ value: string }>) => act({ speed: e.detail.value })) as EventListener);
    setProperties(element, { choices: CHOICES });
    return element;
};

const dialog = (act: (change: Partial<Shown>) => void) => {
    const element = document.createElement("hp-dialog");
    element.setAttribute("title", "Forget this network?");
    element.setAttribute("message", "The password will have to be typed again.");
    setProperties(element, {
        buttons: [
            { value: "forget", label: "Forget", kind: "negative" },
            { value: "keep", label: "Keep it" },
        ],
    });
    const answer = (value: string) => act({ dialog: false, answered: `Chose: ${value}` });
    element.addEventListener("choose", ((e: CustomEvent<{ value: string }>) => answer(e.detail.value)) as EventListener);
    element.addEventListener("dismiss", () => act({ dialog: false, answered: "Dismissed" }));
    return element;
};

const view = (shown: Shown, act: (change: Partial<Shown>) => void) => html`
    <div class="hp-card">
        <hp-header title="Kit" back></hp-header>
        <div class="hp-body">
            ${note("Every control, in each state. This card is the kit itself, so it cannot go stale.")}

            ${section("Buttons", html`
                <div class="hp-list">
                    <div class="hp-row">
                        <hp-button label="Plain" @press=${() => act({ pressed: "Plain" })}></hp-button>
                    </div>
                    <div class="hp-row">
                        <hp-button label="Affirmative" kind="affirmative"
                                   @press=${() => act({ pressed: "Affirmative" })}></hp-button>
                    </div>
                    <div class="hp-row">
                        <hp-button label="Negative" kind="negative"
                                   @press=${() => act({ pressed: "Negative" })}></hp-button>
                    </div>
                    <div class="hp-row">
                        <hp-button label="Disabled" disabled></hp-button>
                    </div>
                </div>
                ${shown.pressed ? note(`Last pressed: ${shown.pressed}`) : ""}`)}

            ${section("Toggles", html`
                <div class="hp-list">
                    <div class="hp-row">
                        <div class="hp-row-text"><div class="hp-row-title">Answers with an event</div>
                            <div class="hp-row-detail">The card decides whether it becomes the new state</div></div>
                        <hp-toggle ?on=${shown.toggled}
                                   @toggle=${(e: CustomEvent<{ on: boolean }>) => act({ toggled: e.detail.on })}>
                        </hp-toggle>
                    </div>
                    <div class="hp-row">
                        <div class="hp-row-text"><div class="hp-row-title">With its own words</div></div>
                        <hp-toggle on label-on="Yes" label-off="No"></hp-toggle>
                    </div>
                    <div class="hp-row">
                        <div class="hp-row-text"><div class="hp-row-title">Disabled</div></div>
                        <hp-toggle disabled></hp-toggle>
                    </div>
                </div>`)}

            ${section("Rows", html`
                <div class="hp-list">
                    <hp-row title="One line"></hp-row>
                    <hp-row title="Two lines" detail="The second one is the detail"></hp-row>
                    <hp-row title="With something on the right" detail="A network, say">
                        <hp-toggle on></hp-toggle>
                    </hp-row>
                    <hp-row title="A long title that has to be cut rather than pushed off the row"
                            detail="Ellipsis, not overflow"></hp-row>
                </div>`)}

            ${section("Fields and checks", html`
                <div class="hp-list">
                    <hp-field label="Network name" placeholder="Type here"
                              @change=${(e: CustomEvent<{ value: string }>) => act({ typed: e.detail.value })}>
                    </hp-field>
                    <hp-field label="Password" type="password" placeholder="Hidden while typing"></hp-field>
                    <div class="hp-row">
                        <div class="hp-row-text"><div class="hp-row-title">A checkbox</div>
                            <div class="hp-row-detail">${shown.checked ? "Ticked" : "Not ticked"}</div></div>
                        <hp-check ?checked=${shown.checked}
                                  @change=${(e: CustomEvent<{ checked: boolean }>) => act({ checked: e.detail.checked })}>
                        </hp-check>
                    </div>
                    <div class="hp-row">
                        <div class="hp-row-text"><div class="hp-row-title">Disabled</div></div>
                        <hp-check disabled></hp-check>
                    </div>
                </div>
                ${shown.typed ? note(t("Typed: #{what}", { what: shown.typed })) : ""}`)}

            ${section("Choosing one of several", html`
                <div class="hp-list">
                    ${selector(shown, act)}
                </div>`)}

            ${section("Waiting and failing", html`
                <div class="hp-list">
                    <hp-spinner label="Always with words beside it"></hp-spinner>
                    <hp-progress value="40" label="Downloading"></hp-progress>
                </div>
                ${error("A failure says what the service said, never the uri.")}`)}

            ${section("Asking before doing", html`
                <div class="hp-list">
                    <div class="hp-row">
                        <div class="hp-row-text"><div class="hp-row-title">A dialog</div>
                            <div class="hp-row-detail">${shown.answered || "Nothing chosen yet"}</div></div>
                        <hp-button label="Open" @press=${() => act({ dialog: true })}></hp-button>
                    </div>
                </div>`)}

            ${section("What WebAppMgr says to the card", html`
                <div class="hp-list">
                    ${shown.life.length === 0
                        ? html`<div class="hp-row"><div class="hp-row-text">
                                 <div class="hp-row-title">Nothing yet</div>
                                 <div class="hp-row-detail">Send the card away and bring it back</div></div></div>`
                        : shown.life.map((line) => html`<hp-row title=${line}></hp-row>`)}
                </div>`)}

            ${shown.dialog ? dialog(act) : ""}
        </div>
    </div>`;

const start = () => {
    useStyles(styles);
    const root = document.getElementById("card") ?? document.body;
    let shown: Shown = {
        toggled: true, pressed: "", checked: true, typed: "", speed: "ask",
        dialog: false, answered: "", life: [],
    };
    const draw = () => render(view(shown, (change) => {
        shown = { ...shown, ...change };
        draw();
    }), root);
    draw();

    // The card's own life, which is what makes it a card and not a page.
    const app = createPalmSystemApp({ window: globalThis as never });
    const note = (what: string) => {
        shown = { ...shown, life: [...shown.life, what].slice(-6) };
        draw();
    };
    app.on("activated", () => note("Brought to the front"));
    app.on("deactivated", () => note("Sent away"));
    app.on("relaunched", (params) => note(`Relaunched with ${JSON.stringify(params)}`));
    app.on("keyboard", (up) => note(up ? "Keyboard came up" : "Keyboard went away"));
    app.on("back", () => note("Back"));
    app.ready();
};

start();
