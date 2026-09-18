// The kit: HP's controls, written as functions, published as elements.
//
// Each one is an ordinary custom element, so a card uses it as markup and
// anything else could too:
//
//     <wos-header title="Wi-Fi"></wos-header>
//     <wos-toggle on label-on="On" label-off="Off"></wos-toggle>
//
// What they look like is kit.css, section by section. What they do is here, and
// it is always the same shape: read the properties, draw, and say what
// happened with an event.

import { defineElement, html, useStyles, type TemplateResult } from "../element.ts";
import styles from "../kit.css";

// The kit dresses itself, here, at the moment it is defined.
//
// This used to be startCard's job, which meant a control put on a page by
// anything other than our runtime -- React, an enyo shim (#56), a plain
// document.createElement -- came out with no styles at all. That was an
// oversight rather than a design: importing this file is what defines the
// elements, so importing this file is what must style them. Whoever uses
// <wos-toggle> is already here by definition.
//
// The sheet in element.ts is made empty and filled in now, so an element the
// page built before this module ran has already adopted this very object and
// is styled by this line too.
useStyles(styles);

// §1 Header. `back` adds HP's back arrow, which emits "back"; whatever the
// card puts inside sits at the right, where the Wi-Fi card's radio switch goes.
// `light` is HP's other toolbar -- the one its settings cards use -- and `icon`
// is the picture beside the title, as the Wi-Fi card has.
defineElement<{ title: string; back: boolean; light: boolean; icon: string }>(
    "wos-header",
    { title: String, back: Boolean, light: Boolean, icon: String },
    ({ title, back, light, icon }, { emit }) => html`
        <header class="wos-header ${light ? "light" : ""}">
            ${back ? html`<button class="wos-header-back" @click=${() => emit("back")}>&#9664;</button>` : ""}
            <span class="wos-header-title">
                ${icon ? html`<img class="wos-header-icon" src=${icon} alt="">` : ""}
                <span>${title}</span>
            </span>
            <span class="wos-header-end"><slot></slot></span>
        </header>`,
);

// §2 Button. "affirmative" and "negative" are HP's two loud kinds.
defineElement<{ label: string; kind: string; disabled: boolean }>(
    "wos-button",
    { label: String, kind: String, disabled: Boolean },
    ({ label, kind, disabled }, { emit }) => html`
        <button class="wos-button ${kind ?? ""}" ?disabled=${disabled}
                @click=${() => emit("press")}>${label}</button>`,
);

// §4 Toggle. The card is told what the user asked for; it decides whether that
// becomes the new state, because on a device the answer comes from a service.
defineElement<{ on: boolean; labelOn: string; labelOff: string; disabled: boolean }>(
    "wos-toggle",
    { on: Boolean, labelOn: String, labelOff: String, disabled: Boolean },
    ({ on, labelOn, labelOff, disabled }, { emit }) => html`
        <button class="wos-toggle ${on ? "on" : "off"}" ?disabled=${disabled}
                role="switch" aria-checked=${on ? "true" : "false"}
                @click=${() => emit("toggle", { on: !on })}>
            <span class="wos-toggle-knob"></span>
            <span class="wos-toggle-label">${on ? labelOn || "On" : labelOff || "Off"}</span>
        </button>`,
);

// §5 Spinner, with the line HP always put beside it: a spinner with no words
// is a card that has stopped answering.
defineElement<{ label: string }>(
    "wos-spinner",
    { label: String },
    ({ label }) => html`
        <div class="wos-spinner-row">
            <span class="wos-spinner"></span>
            <span>${label}</span>
        </div>`,
);

// §3 A group of rows under HP's caption bar, which is what enyo's RowGroup
// drew: the title sits in a grey bar joined to the top of the list.
defineElement<{ title: string }>(
    "wos-group",
    { title: String },
    ({ title }) => html`
        <div class="wos-group ${title ? "captioned" : ""}">
            ${title ? html`<div class="wos-group-caption">${title}</div>` : ""}
            <div class="wos-list"><slot></slot></div>
        </div>`,
);

// A row. `title` and `detail` are the two lines HP's lists have; whatever the
// card puts inside goes to the right of them, and what it puts in the "lead"
// slot goes before them. Only the row's own part of it
// selects the row: a toggle in a Wi-Fi row would otherwise turn the radio on
// and open the network at the same time.
defineElement<{ title: string; detail: string; strong: boolean }>(
    "wos-row",
    { title: String, detail: String, strong: Boolean },
    ({ title, detail, strong }, { emit }) => html`
        <div class="wos-row">
            <!-- What goes before the words: the plus HP drew at the left of
                 "Join Network", an avatar, a status light. -->
            <slot name="lead"></slot>
            ${title || detail
                ? html`
                    <div class="wos-row-text" @click=${() => emit("select")}>
                        <div class="wos-row-title ${strong ? "strong" : ""}">${title}</div>
                        ${detail ? html`<div class="wos-row-detail">${detail}</div>` : ""}
                    </div>`
                : ""}
            <slot></slot>
        </div>`,
);

// The pieces a card draws itself, where an element of its own would only get
// in the way.
export const note = (text: string): TemplateResult => html`<p class="wos-note">${text}</p>`;
export const error = (text: string): TemplateResult => html`<p class="wos-error">${text}</p>`;

// §7 Text field. The card is told what was typed as it is typed, and when the
// user is done with it.
defineElement<{ label: string; value: string; placeholder: string; type: string; disabled: boolean }>(
    "wos-field",
    { label: String, value: String, placeholder: String, type: String, disabled: Boolean },
    ({ label, value, placeholder, type, disabled }, { emit }) => html`
        <label class="wos-field">
            ${label ? html`<span class="wos-field-label">${label}</span>` : ""}
            <input class="wos-field-input" .value=${value} type=${type || "text"}
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
    "wos-check",
    { checked: Boolean, disabled: Boolean },
    ({ checked, disabled }, { emit }) => html`
        <button class="wos-check ${checked ? "checked" : ""}" ?disabled=${disabled}
                role="checkbox" aria-checked=${checked ? "true" : "false"}
                @click=${() => emit("change", { checked: !checked })}>
            ${checked ? html`<span class="wos-check-tick">&#10003;</span>` : ""}
        </button>`,
);

// §9 List selector: the row that shows the chosen one and opens HP's drawer of
// choices under it. `choices` is set as a property, not an attribute. Like
// every other control here, it says what the user asked for -- "open" and
// "choose" -- and the card decides what that makes true.
defineElement<{ label: string; value: string; choices: { value: string; label: string }[]; open: boolean }>(
    "wos-selector",
    { label: String, value: String, choices: Object, open: Boolean },
    ({ label, value, choices, open }, { emit }) => {
        const list = Array.isArray(choices) ? choices : [];
        const chosen = list.find((choice) => choice.value === value);
        return html`
            <div class="wos-selector">
                <div class="wos-row" @click=${() => emit("open", { open: !open })}>
                    <div class="wos-row-text"><div class="wos-row-title">${label ?? ""}</div></div>
                    <span class="wos-selector-value">${chosen?.label ?? value}</span>
                    <span class="wos-selector-arrow ${open ? "open" : ""}">&#9662;</span>
                </div>
                ${open
                    ? html`<div class="wos-selector-drawer">
                        ${list.map((choice) => html`
                            <div class="wos-row wos-selector-choice ${choice.value === value ? "chosen" : ""}"
                                 @click=${() => emit("choose", { value: choice.value })}>
                                <div class="wos-row-text"><div class="wos-row-title">${choice.label}</div></div>
                                ${choice.value === value ? html`<span class="wos-selector-tick">&#10003;</span>` : ""}
                            </div>`)}
                      </div>`
                    : ""}
            </div>`;
    },
);

// §10 Dialog: HP's modal, with its title, its message and its buttons. The
// card says which button was pressed by its value.
defineElement<{ title: string; message: string; buttons: { value: string; label: string; kind?: string }[] }>(
    "wos-dialog",
    { title: String, message: String, buttons: Object },
    ({ title, message, buttons }, { emit }) => html`
        <div class="wos-dialog-shade" @click=${() => emit("dismiss")}>
            <div class="wos-dialog" @click=${(event: Event) => event.stopPropagation()}>
                ${title ? html`<div class="wos-dialog-title">${title}</div>` : ""}
                ${message ? html`<div class="wos-dialog-message">${message}</div>` : ""}
                <div class="wos-dialog-buttons">
                    ${(Array.isArray(buttons) ? buttons : []).map((button) => html`
                        <button class="wos-button ${button.kind ?? ""}"
                                @click=${() => emit("choose", { value: button.value })}>${button.label}</button>`)}
                </div>
            </div>
        </div>`,
);

// §11 Progress, for the things that take long enough to show how far along
// they are.
defineElement<{ value: number; label: string }>(
    "wos-progress",
    { value: Number, label: String },
    ({ value, label }) => html`
        <div class="wos-progress-row">
            ${label ? html`<span class="wos-progress-label">${label}</span>` : ""}
            <span class="wos-progress"><span class="wos-progress-bar"
                  style="width: ${Math.max(0, Math.min(100, Number(value) || 0))}%"></span></span>
        </div>`,
);

// §12 A row that is swiped aside to delete what it holds, with HP's inline
// confirmation behind it: the swipe uncovers "Delete", and only that deletes.
// enyo's SwipeableItem, whose `confirmRequired` is the same switch: without it
// the swipe itself deletes, which is how HP's lists that cannot be undone
// behaved.
defineElement<{ title: string; detail: string; confirm: string; instant: boolean; open: boolean; strong: boolean }>(
    "wos-swipe-row",
    { title: String, detail: String, confirm: String, instant: Boolean, open: Boolean, strong: Boolean },
    ({ title, detail, confirm, instant, open, strong }, { emit }) => {
        // A swipe is a drag that got far enough to mean it: the card is told
        // what the user asked for, and decides.
        let from = 0;
        const start = (event: PointerEvent) => {
            from = event.clientX;
        };
        const end = (event: PointerEvent) => {
            const moved = from - event.clientX;
            from = 0;
            if (moved < 40) {
                return;
            }
            if (instant) {
                emit("remove");
            } else {
                emit("open", { open: true });
            }
        };
        return html`
            <div class="wos-swipe-row ${open ? "open" : ""}"
                 @pointerdown=${start} @pointerup=${end}>
                <div class="wos-row">
                    <div class="wos-row-text" @click=${() => emit("select")}>
                        <div class="wos-row-title ${strong ? "strong" : ""}">${title}</div>
                        ${detail ? html`<div class="wos-row-detail">${detail}</div>` : ""}
                    </div>
                    <slot></slot>
                </div>
                ${open
                    ? html`
                        <div class="wos-swipe-confirm">
                            <button class="wos-button negative"
                                    @click=${() => emit("remove")}>${confirm || "Delete"}</button>
                            <button class="wos-button"
                                    @click=${() => emit("open", { open: false })}>Cancel</button>
                        </div>`
                    : ""}
            </div>`;
    },
);

// §13 The app menu: what is behind the card's name in the top-left corner, and
// what the system opens with the menu key. WebAppMgr says when it was asked
// for (Mojo's openAppMenu, which AppService reports), so the card decides
// whether it is open, as with every other control here.
defineElement<{ open: boolean; items: { value: string; label: string; disabled?: boolean }[] }>(
    "wos-app-menu",
    { open: Boolean, items: Object },
    ({ open, items }, { emit }) => {
        if (!open) {
            return html``;
        }
        const list = Array.isArray(items) ? items : [];
        return html`
            <div class="wos-menu-shade" @click=${() => emit("close")}>
                <div class="wos-menu" @click=${(event: Event) => event.stopPropagation()}>
                    ${list.map((item) => html`
                        <button class="wos-menu-item" ?disabled=${item.disabled}
                                @click=${() => emit("choose", { value: item.value })}>${item.label}</button>`)}
                </div>
            </div>`;
    },
);

// §14 A button that shows it is working: enyo's ActivityButton, which is what
// HP put on "Join" so a network that takes ten seconds does not look ignored.
defineElement<{ label: string; kind: string; busy: boolean; disabled: boolean }>(
    "wos-activity-button",
    { label: String, kind: String, busy: Boolean, disabled: Boolean },
    ({ label, kind, busy, disabled }, { emit }) => html`
        <button class="wos-button ${kind}" ?disabled=${disabled || busy}
                @click=${() => emit("press")}>
            <span class="wos-button-label">${label}</span>
            ${busy ? html`<span class="wos-button-spinner"></span>` : ""}
        </button>`,
);

// §15 One of a few, chosen in the row itself: enyo's ListSelector, which is
// what HP used for "When Device Sleeps" and for most settings with two or
// three answers. The drawer of wos-selector is for longer lists.
defineElement<{ label: string; value: string; choices: { value: string; label: string }[] }>(
    "wos-choice",
    { label: String, value: String, choices: Object },
    ({ label, value, choices }, { emit }) => {
        const list = Array.isArray(choices) ? choices : [];
        return html`
            <div class="wos-row">
                ${label ? html`<div class="wos-row-text"><div class="wos-row-title">${label}</div></div>` : ""}
                <div class="wos-choice">
                    ${list.map((choice) => html`
                        <button class="wos-choice-one ${choice.value === value ? "chosen" : ""}"
                                @click=${() => emit("choose", { value: choice.value })}>${choice.label}</button>`)}
                </div>
            </div>`;
    },
);

// §16 A slider: volume, brightness, where a video is. enyo's Slider, whose two
// events this keeps -- "changing" while the finger is down, "change" when it
// lifts -- because that is the difference between showing the new brightness
// as it is dragged and writing it to the service on every pixel.
//
// Tapping the bar moves it there, as enyo's tapPosition did.
defineElement<{ value: number; min: number; max: number; disabled: boolean }>(
    "wos-slider",
    { value: Number, min: Number, max: Number, disabled: Boolean },
    ({ value, min, max, disabled }, { emit, element }) => {
        const low = Number.isFinite(min) ? min : 0;
        const high = Number.isFinite(max) && max > low ? max : 100;
        const at = Math.min(high, Math.max(low, Number(value) || 0));
        const part = (at - low) / (high - low);

        const valueAt = (clientX: number): number => {
            const bar = element.shadowRoot?.querySelector(".wos-slider-bar");
            const box = bar?.getBoundingClientRect();
            if (!box || box.width === 0) {
                return at;
            }
            const along = Math.min(1, Math.max(0, (clientX - box.left) / box.width));
            return Math.round(low + along * (high - low));
        };

        let dragging = false;
        const down = (event: PointerEvent) => {
            if (disabled) {
                return;
            }
            dragging = true;
            // Keeping the finger's events coming even if it leaves the bar is
            // worth having and not worth failing over: a pointer id that is
            // not being tracked -- a synthetic event, another engine --
            // refuses, and that must not swallow the drag itself.
            try {
                (event.target as Element).setPointerCapture?.(event.pointerId);
            } catch {
                // Then the events stop at the bar's edge, which is still a drag.
            }
            emit("changing", { value: valueAt(event.clientX) });
        };
        const move = (event: PointerEvent) => {
            if (dragging) {
                emit("changing", { value: valueAt(event.clientX) });
            }
        };
        const up = (event: PointerEvent) => {
            if (!dragging) {
                return;
            }
            dragging = false;
            emit("change", { value: valueAt(event.clientX) });
        };

        return html`
            <div class="wos-slider ${disabled ? "disabled" : ""}"
                 role="slider" aria-valuenow=${at} aria-valuemin=${low} aria-valuemax=${high}
                 @pointerdown=${down} @pointermove=${move} @pointerup=${up} @pointercancel=${up}>
                <div class="wos-slider-bar">
                    <div class="wos-slider-filled" style="width: ${Math.round(part * 100)}%"></div>
                    <div class="wos-slider-knob" style="left: ${Math.round(part * 100)}%"></div>
                </div>
            </div>`;
    },
);
