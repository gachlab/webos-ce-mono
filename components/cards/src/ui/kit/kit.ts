// The kit: HP's controls, written as functions, published as elements.
//
// Each one is an ordinary custom element, so a card uses it as markup and
// anything else could too:
//
//     <hp-header title="Wi-Fi"></hp-header>
//     <hp-toggle on label-on="On" label-off="Off"></hp-toggle>
//
// What they look like is kit.css, section by section. What they do is here, and
// it is always the same shape: read the properties, draw, and say what
// happened with an event.

import { defineElement, html, type TemplateResult } from "#ui/element.ts";

// §1 Header. `back` adds HP's back arrow, which emits "back".
defineElement<{ title: string; back: boolean }>(
    "hp-header",
    { title: String, back: Boolean },
    ({ title, back }, { emit }) => html`
        <header class="hp-header">
            ${back ? html`<button class="hp-header-back" @click=${() => emit("back")}>&#9664;</button>` : ""}
            <span>${title}</span>
        </header>`,
);

// §2 Button. "affirmative" and "negative" are HP's two loud kinds.
defineElement<{ label: string; kind: string; disabled: boolean }>(
    "hp-button",
    { label: String, kind: String, disabled: Boolean },
    ({ label, kind, disabled }, { emit }) => html`
        <button class="hp-button ${kind ?? ""}" ?disabled=${disabled}
                @click=${() => emit("press")}>${label}</button>`,
);

// §4 Toggle. The card is told what the user asked for; it decides whether that
// becomes the new state, because on a device the answer comes from a service.
defineElement<{ on: boolean; labelOn: string; labelOff: string; disabled: boolean }>(
    "hp-toggle",
    { on: Boolean, labelOn: String, labelOff: String, disabled: Boolean },
    ({ on, labelOn, labelOff, disabled }, { emit }) => html`
        <button class="hp-toggle ${on ? "on" : "off"}" ?disabled=${disabled}
                role="switch" aria-checked=${on ? "true" : "false"}
                @click=${() => emit("toggle", { on: !on })}>
            <span class="hp-toggle-knob"></span>
            <span class="hp-toggle-label">${on ? labelOn || "On" : labelOff || "Off"}</span>
        </button>`,
);

// §5 Spinner, with the line HP always put beside it: a spinner with no words
// is a card that has stopped answering.
defineElement<{ label: string }>(
    "hp-spinner",
    { label: String },
    ({ label }) => html`
        <div class="hp-spinner-row">
            <span class="hp-spinner"></span>
            <span>${label}</span>
        </div>`,
);

// §3 A group of rows, with HP's small upper-case title.
defineElement<{ title: string }>(
    "hp-group",
    { title: String },
    ({ title }) => html`
        ${title ? html`<div class="hp-group-title">${title}</div>` : ""}
        <div class="hp-list"><slot></slot></div>`,
);

// A row. `title` and `detail` are the two lines HP's lists have; whatever the
// card puts inside goes to the right of them. Only the row's own part of it
// selects the row: a toggle in a Wi-Fi row would otherwise turn the radio on
// and open the network at the same time.
defineElement<{ title: string; detail: string }>(
    "hp-row",
    { title: String, detail: String },
    ({ title, detail }, { emit }) => html`
        <div class="hp-row">
            ${title || detail
                ? html`
                    <div class="hp-row-text" @click=${() => emit("select")}>
                        <div class="hp-row-title">${title}</div>
                        ${detail ? html`<div class="hp-row-detail">${detail}</div>` : ""}
                    </div>`
                : ""}
            <slot></slot>
        </div>`,
);

// The pieces a card draws itself, where an element of its own would only get
// in the way.
export const note = (text: string): TemplateResult => html`<p class="hp-note">${text}</p>`;
export const error = (text: string): TemplateResult => html`<p class="hp-error">${text}</p>`;

// §7 Text field. The card is told what was typed as it is typed, and when the
// user is done with it.
defineElement<{ label: string; value: string; placeholder: string; type: string; disabled: boolean }>(
    "hp-field",
    { label: String, value: String, placeholder: String, type: String, disabled: Boolean },
    ({ label, value, placeholder, type, disabled }, { emit }) => html`
        <label class="hp-field">
            ${label ? html`<span class="hp-field-label">${label}</span>` : ""}
            <input class="hp-field-input" .value=${value} type=${type || "text"}
                   placeholder=${placeholder} ?disabled=${disabled}
                   @input=${(event: Event) => emit("change", { value: (event.target as HTMLInputElement).value })}
                   @keydown=${(event: KeyboardEvent) => {
                       if (event.key === "Enter") {
                           emit("done", { value: (event.target as HTMLInputElement).value });
                       }
                   }}>
        </label>`,
);

// §8 Checkbox, HP's tick in a rounded square.
defineElement<{ checked: boolean; disabled: boolean }>(
    "hp-check",
    { checked: Boolean, disabled: Boolean },
    ({ checked, disabled }, { emit }) => html`
        <button class="hp-check ${checked ? "checked" : ""}" ?disabled=${disabled}
                role="checkbox" aria-checked=${checked ? "true" : "false"}
                @click=${() => emit("change", { checked: !checked })}>
            ${checked ? html`<span class="hp-check-tick">&#10003;</span>` : ""}
        </button>`,
);

// §9 List selector: the row that shows the chosen one and opens HP's drawer of
// choices under it. `choices` is set as a property, not an attribute. Like
// every other control here, it says what the user asked for -- "open" and
// "choose" -- and the card decides what that makes true.
defineElement<{ label: string; value: string; choices: { value: string; label: string }[]; open: boolean }>(
    "hp-selector",
    { label: String, value: String, choices: Object, open: Boolean },
    ({ label, value, choices, open }, { emit }) => {
        const list = Array.isArray(choices) ? choices : [];
        const chosen = list.find((choice) => choice.value === value);
        return html`
            <div class="hp-selector">
                <div class="hp-row" @click=${() => emit("open", { open: !open })}>
                    <div class="hp-row-text"><div class="hp-row-title">${label ?? ""}</div></div>
                    <span class="hp-selector-value">${chosen?.label ?? value}</span>
                    <span class="hp-selector-arrow ${open ? "open" : ""}">&#9662;</span>
                </div>
                ${open
                    ? html`<div class="hp-selector-drawer">
                        ${list.map((choice) => html`
                            <div class="hp-row hp-selector-choice ${choice.value === value ? "chosen" : ""}"
                                 @click=${() => emit("choose", { value: choice.value })}>
                                <div class="hp-row-text"><div class="hp-row-title">${choice.label}</div></div>
                                ${choice.value === value ? html`<span class="hp-selector-tick">&#10003;</span>` : ""}
                            </div>`)}
                      </div>`
                    : ""}
            </div>`;
    },
);

// §10 Dialog: HP's modal, with its title, its message and its buttons. The
// card says which button was pressed by its value.
defineElement<{ title: string; message: string; buttons: { value: string; label: string; kind?: string }[] }>(
    "hp-dialog",
    { title: String, message: String, buttons: Object },
    ({ title, message, buttons }, { emit }) => html`
        <div class="hp-dialog-shade" @click=${() => emit("dismiss")}>
            <div class="hp-dialog" @click=${(event: Event) => event.stopPropagation()}>
                ${title ? html`<div class="hp-dialog-title">${title}</div>` : ""}
                ${message ? html`<div class="hp-dialog-message">${message}</div>` : ""}
                <div class="hp-dialog-buttons">
                    ${(Array.isArray(buttons) ? buttons : []).map((button) => html`
                        <button class="hp-button ${button.kind ?? ""}"
                                @click=${() => emit("choose", { value: button.value })}>${button.label}</button>`)}
                </div>
            </div>
        </div>`,
);

// §11 Progress, for the things that take long enough to show how far along
// they are.
defineElement<{ value: number; label: string }>(
    "hp-progress",
    { value: Number, label: String },
    ({ value, label }) => html`
        <div class="hp-progress-row">
            ${label ? html`<span class="hp-progress-label">${label}</span>` : ""}
            <span class="hp-progress"><span class="hp-progress-bar"
                  style="width: ${Math.max(0, Math.min(100, Number(value) || 0))}%"></span></span>
        </div>`,
);
