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
                <span class="wos-header-label">${title}</span>
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
defineElement<{ title: string; detail: string; strong: boolean; selected: boolean }>(
    "wos-row",
    { title: String, detail: String, strong: Boolean, selected: Boolean },
    ({ title, detail, strong, selected }, { emit }) => html`
        <div class="wos-row ${selected ? "selected" : ""}">
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
            ${checked ? html`<span class="wos-check-tick" aria-hidden="true"></span>` : ""}
        </button>`,
);

// §8b Info. The (i) at the right of a list row that opens details without
// selecting the row -- HP's info-icon-sprite.png on the VPN card. Emits "press".
defineElement<{ disabled: boolean }>(
    "wos-info",
    { disabled: Boolean },
    ({ disabled }, { emit }) => html`
        <button class="wos-info" type="button" ?disabled=${disabled}
                aria-label="Details"
                @click=${(event: Event) => {
                    event.stopPropagation();
                    emit("press");
                }}>
            <span class="wos-info-mark" aria-hidden="true">
                <span class="wos-info-dot"></span>
                <span class="wos-info-stem"></span>
            </span>
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
        const caption = chosen?.label ?? value;
        // No label → value is the row title (VPN Add). With a label → value at
        // the right, as Wi-Fi sleep and the showcase do.
        const named = !!(label && label.length > 0);
        return html`
            <div class="wos-selector">
                <div class="wos-row" @click=${() => emit("open", { open: !open })}>
                    ${named
                        ? html`
                            <div class="wos-row-text"><div class="wos-row-title">${label}</div></div>
                            <span class="wos-selector-value">${caption}</span>`
                        : html`
                            <div class="wos-row-text"><div class="wos-row-title">${caption}</div></div>`}
                    <span class="wos-selector-arrow ${open ? "open" : ""}" aria-hidden="true"></span>
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
//
// Inside the dialog the keyboard is the dialog's, not the card's: Escape
// dismisses it (rather than the back gesture closing the whole card), and the
// focus starts on the first button and is kept from wandering out behind the
// shade -- enyo's ModalDialog trapped it the same way. The shade is not in the
// tab order; only the dialog is.
defineElement<{ title: string; message: string; buttons: { value: string; label: string; kind?: string }[] }>(
    "wos-dialog",
    { title: String, message: String, buttons: Object },
    ({ title, message, buttons }, { emit, element, firstPaint, onRemoved }) => {
        if (firstPaint()) {
            queueMicrotask(() => {
                element.shadowRoot?.querySelector<HTMLElement>(".wos-dialog .wos-button")?.focus();
            });
            // Escape closes the dialog, not the card. The card's back gesture
            // is a document keydown (see connectCard), so this has to win over
            // it wherever the focus happens to be -- including the moment
            // before the focus above has landed, which a handler on the shade
            // would miss and let the card close. A capture listener on the
            // document sees it first; onRemoved takes it away with the dialog.
            if (typeof document !== "undefined") {
                const onEscape = (event: KeyboardEvent): void => {
                    if (event.key === "Escape") {
                        event.stopPropagation();
                        emit("dismiss");
                    }
                };
                document.addEventListener("keydown", onEscape, true);
                onRemoved(() => document.removeEventListener("keydown", onEscape, true));
            }
        }
        const onKey = (event: KeyboardEvent): void => {
            if (event.key !== "Tab") {
                return;
            }
            // Keep the focus inside the dialog: past the last button wraps to
            // the first, and Shift+Tab off the first wraps to the last.
            const focusable = Array.from(
                element.shadowRoot?.querySelectorAll<HTMLElement>(".wos-dialog .wos-button") ?? [],
            );
            if (focusable.length === 0) {
                return;
            }
            const first = focusable[0]!;
            const last = focusable[focusable.length - 1]!;
            const here = element.shadowRoot?.activeElement;
            if (event.shiftKey && here === first) {
                event.preventDefault();
                last.focus();
            } else if (!event.shiftKey && here === last) {
                event.preventDefault();
                first.focus();
            }
        };
        return html`
            <div class="wos-dialog-shade" @click=${() => emit("dismiss")} @keydown=${onKey}>
                <div class="wos-dialog" @click=${(event: Event) => event.stopPropagation()}>
                    ${title ? html`<div class="wos-dialog-title">${title}</div>` : ""}
                    ${message ? html`<div class="wos-dialog-message">${message}</div>` : ""}
                    <div class="wos-dialog-buttons">
                        ${(Array.isArray(buttons) ? buttons : []).map((button) => html`
                            <button class="wos-button ${button.kind ?? ""}"
                                    @click=${() => emit("choose", { value: button.value })}>${button.label}</button>`)}
                    </div>
                </div>
            </div>`;
    },
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
defineElement<{ label: string; value: string; choices: { value: string; label: string; disabled?: boolean }[] }>(
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
                                ?disabled=${choice.disabled}
                                @click=${() => {
                                    if (!choice.disabled) {
                                        emit("choose", { value: choice.value });
                                    }
                                }}>${choice.label}</button>`)}
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


// §17 Icon button: enyo's IconButton, the round picture button in a header or
// a toolbar -- the (+) the browser puts on its toolbar, the back-and-forward
// pair. `icon` is a picture the card gives it; `label` is the optional word
// enyo drew under the icon. It says it was pressed and nothing else, as every
// button here does.
defineElement<{ icon: string; label: string; disabled: boolean }>(
    "wos-icon-button",
    { icon: String, label: String, disabled: Boolean },
    ({ icon, label, disabled }, { emit }) => html`
        <button class="wos-icon-button" type="button" ?disabled=${disabled}
                aria-label=${label || "button"}
                @click=${() => emit("press")}>
            <span class="wos-icon-button-face"
                  style=${icon ? `background-image: url(${icon})` : ""}></span>
            ${label ? html`<span class="wos-icon-button-label">${label}</span>` : ""}
        </button>`,
);

// §18 Tabs across the top of a card: enyo's TabGroup, which is a RadioGroup --
// one is chosen at a time, tapping another chooses it. Just Type's
// ALL/CONTACTS/CONTENT/ACTIONS, the tabs on Contacts and Email. It says which
// tab the user asked for and the card decides what that shows, as everything
// here does. `tabs` is set as a property.
defineElement<{ value: string; tabs: { value: string; label: string; disabled?: boolean }[] }>(
    "wos-tab-group",
    { value: String, tabs: Object },
    ({ value, tabs }, { emit }) => {
        const list = Array.isArray(tabs) ? tabs : [];
        return html`
            <div class="wos-tab-group" role="tablist">
                ${list.map((tab) => html`
                    <button class="wos-tab ${tab.value === value ? "chosen" : ""}"
                            role="tab" aria-selected=${tab.value === value ? "true" : "false"}
                            ?disabled=${tab.disabled}
                            @click=${() => {
                                if (tab.value !== value && !tab.disabled) {
                                    emit("choose", { value: tab.value });
                                }
                            }}>${tab.label}</button>`)}
            </div>`;
    },
);

// §19 A divider: enyo's Divider, the captioned line that heads a stretch of a
// list, and its AlphaDivider, the single sticky letter down a long one
// (Contacts, Music). `alpha` is the letter kind; `caption` is the word either
// way. It draws nothing that can be pressed -- a heading is not a control.
defineElement<{ caption: string; alpha: boolean }>(
    "wos-divider",
    { caption: String, alpha: Boolean },
    ({ caption, alpha }) => html`
        <div class="wos-divider ${alpha ? "alpha" : ""}">
            <span class="wos-divider-caption">${caption}</span>
        </div>`,
);

// §20 A long list that makes its rows as they are scrolled to: enyo's
// VirtualList, which is what a card with thousands of rows needs (Contacts,
// Email) and a settings card does not. `rows` is the data, set as a property;
// `rowHeight` is how tall each one is, which is what lets it hold the room for
// the ones not drawn without measuring them. The card gives it a `render` for
// one row's markup, and hears "select" with the row's index.
// §20 A long list that makes its rows as they are scrolled to: enyo's
// VirtualList, which is what a card with thousands of rows needs (Contacts,
// Email) and a settings card does not. `rows` is the data, set as a property;
// `rowHeight` is how tall each one is, which is what lets it hold the room for
// the ones not drawn without measuring them. The card gives it a `render` for
// one row's markup, and hears "activate" with the row's index.
//
// It is not enyo's flyweight -- that reused one control and re-populated it per
// index, which a framework that owns its DOM cannot assume. This keeps only the
// window of rows the viewport shows (plus a margin) in the DOM, and spaces them
// with two struts, so a list of ten thousand costs the rows on screen.
//
// `at` is where it is scrolled to, and it is the list's own -- the scroll
// handler writes it, which is what repaints the window as the user scrolls.
// The framework repaints on a property being set, and a scroll is not a
// property the platform sets, so the list makes one of its own. A card never
// touches it.
defineElement<{
    rows: unknown[];
    rowHeight: number;
    render: (row: unknown, index: number) => TemplateResult;
    overscan: number;
    at: number;
}>(
    "wos-list",
    { rows: Object, rowHeight: Number, render: Object, overscan: Number, at: Number },
    ({ rows, rowHeight, render, overscan, at }, { emit, element, firstPaint }) => {
        const data = Array.isArray(rows) ? rows : [];
        const height = Number.isFinite(rowHeight) && rowHeight > 0 ? rowHeight : 44;
        const margin = Number.isFinite(overscan) && overscan >= 0 ? overscan : 6;
        const draw = typeof render === "function"
            ? render
            : (row: unknown) => html`<wos-row title=${String(row)}></wos-row>`;

        // What is drawn is the slice the port shows, grown by the margin at
        // each end so a flick does not outrun it, held in place by a strut
        // above and the total height below. The scroll offset is `at`, which
        // the scroll handler keeps in step with the port; clientHeight is read
        // off the port because it is not a property.
        const port = element.shadowRoot?.querySelector<HTMLElement>(".wos-list-port");
        const scrolled = Number.isFinite(at) && at >= 0 ? at : 0;
        const visible = port ? port.clientHeight : height * 8;
        const first = Math.max(0, Math.floor(scrolled / height) - margin);
        const count = Math.ceil(visible / height) + margin * 2;
        const last = Math.min(data.length, first + count);
        const window = data.slice(first, last);
        const total = data.length * height;

        // A scroll is not a repaint the platform gives us. Writing the offset
        // to `at` is: it is a declared property, so setting it repaints, and
        // the window above is computed from it. Registered once -- the listener
        // is on a node inside the shadow root, which goes when the element
        // does, so no cleanup is needed.
        if (firstPaint()) {
            queueMicrotask(() => {
                const node = element.shadowRoot?.querySelector<HTMLElement>(".wos-list-port");
                node?.addEventListener("scroll", () => {
                    (element as unknown as { at: number }).at = node.scrollTop;
                    emit("scrolled", { at: node.scrollTop });
                }, { passive: true });
            });
        }

        return html`
            <div class="wos-list-port">
                <div class="wos-list-run" style="height: ${total}px">
                    <div class="wos-list-window" style="transform: translateY(${first * height}px)">
                        ${window.map((row, offset) => {
                            const index = first + offset;
                            return html`
                                <div class="wos-list-row" style="height: ${height}px"
                                     @click=${() => emit("activate", { index })}>
                                    ${draw(row, index)}
                                </div>`;
                        })}
                    </div>
                </div>
            </div>`;
    },
);

// §21 A search field: enyo's SearchInput, the field with a magnifier while it
// is empty and a clear cross once something is in it -- Just Type, Contacts,
// Email. It says what was typed as it is typed, like wos-field, and it says
// "cancel" when the cross is tapped, which is enyo's own event for the field
// being cleared.
defineElement<{ value: string; placeholder: string; disabled: boolean }>(
    "wos-search-field",
    { value: String, placeholder: String, disabled: Boolean },
    ({ value, placeholder, disabled }, { emit }) => {
        const has = !!(value && value.length > 0);
        return html`
            <div class="wos-search ${has ? "has-text" : ""}">
                <span class="wos-search-glass" aria-hidden="true"></span>
                <input class="wos-search-input" .value=${value} type="search"
                       placeholder=${placeholder || "Search"} ?disabled=${disabled}
                       @input=${(event: Event) => emit("change", { value: (event.target as HTMLInputElement).value })}
                       @keydown=${(event: KeyboardEvent) => {
                           if (event.key === "Enter") {
                               emit("done", { value: (event.target as HTMLInputElement).value });
                           }
                           if (event.key === "Escape") {
                               emit("cancel");
                           }
                       }}>
                ${has
                    ? html`<button class="wos-search-clear" type="button" aria-label="Clear"
                                   @click=${() => emit("cancel")}></button>`
                    : ""}
            </div>`;
    },
);

// §22 A field that grows with what is typed: enyo had no TextArea kind -- its
// growing field was RichText, a contenteditable capped at a max height, after
// which it scrolls. Memos, the body of a message, Email. `rows` is how tall it
// starts; `maxRows` is where it stops growing and starts scrolling. It says
// what was typed as wos-field does.
defineElement<{ value: string; placeholder: string; rows: number; maxRows: number; disabled: boolean }>(
    "wos-text-area",
    { value: String, placeholder: String, rows: Number, maxRows: Number, disabled: Boolean },
    ({ value, placeholder, rows, maxRows, disabled }, { emit }) => {
        const min = Number.isFinite(rows) && rows > 0 ? rows : 2;
        const max = Number.isFinite(maxRows) && maxRows >= min ? maxRows : 8;
        // Grow to the content, up to the cap, then let it scroll: measured off
        // the field itself so it does not depend on a line-height guess.
        const grow = (event: Event): void => {
            const area = event.target as HTMLTextAreaElement;
            area.style.height = "auto";
            const line = parseFloat(getComputedStyle(area).lineHeight) || 20;
            const capped = Math.min(area.scrollHeight, line * max);
            area.style.height = `${capped}px`;
            area.style.overflowY = area.scrollHeight > capped ? "auto" : "hidden";
        };
        return html`
            <textarea class="wos-text-area" rows=${min} .value=${value}
                      placeholder=${placeholder} ?disabled=${disabled}
                      @input=${(event: Event) => {
                          grow(event);
                          emit("change", { value: (event.target as HTMLTextAreaElement).value });
                      }}></textarea>`;
    },
);

// §23 A picker: enyo's IntegerPicker, the pill that opens a wheel of values
// (Date & Time, Clock, an alarm's minutes). The general wheel; the date and
// time pickers HP had are three of these side by side, which a card composes.
// `min`..`max` is the range; it says what was picked with "change". Tapping the
// pill opens the wheel, and the card decides it is open, as with the selector.
//
// Whether each picker's wheel was open at its last paint, so the wheel is
// scrolled to the value only as it opens and not on every repaint after.
const pickerWasOpen = new WeakMap<HTMLElement, boolean>();

defineElement<{ label: string; value: number; min: number; max: number; open: boolean }>(
    "wos-picker",
    { label: String, value: Number, min: Number, max: Number, open: Boolean },
    ({ label, value, min, max, open }, { emit, element }) => {
        const low = Number.isFinite(min) ? min : 0;
        const high = Number.isFinite(max) && max >= low ? max : 9;
        const at = Math.min(high, Math.max(low, Number(value) || 0));
        const values: number[] = [];
        for (let n = low; n <= high; n++) {
            values.push(n);
        }
        // The wheel scrolls to the chosen value when it opens -- on the
        // transition into open, not on every repaint while open. Scrolling on
        // each repaint would snap the wheel back under a finger that is
        // dragging it. `pickerWasOpen` remembers the last state per element.
        const wasOpen = pickerWasOpen.get(element) ?? false;
        pickerWasOpen.set(element, open);
        if (open && !wasOpen) {
            queueMicrotask(() => {
                const chosen = element.shadowRoot?.querySelector<HTMLElement>(".wos-picker-item.chosen");
                chosen?.scrollIntoView({ block: "center" });
            });
        }
        return html`
            <div class="wos-picker">
                ${label ? html`<span class="wos-picker-label">${label}</span>` : ""}
                <button class="wos-picker-pill" type="button"
                        @click=${() => emit("open", { open: !open })}>${at}</button>
                ${open
                    ? html`<div class="wos-picker-wheel">
                        ${values.map((n) => html`
                            <button class="wos-picker-item ${n === at ? "chosen" : ""}" type="button"
                                    @click=${() => emit("change", { value: n })}>${n}</button>`)}
                      </div>`
                    : ""}
            </div>`;
    },
);

// §24 A list that opens where it was tapped: enyo's PopupList, the choices that
// appear over the row rather than in a drawer under it (Browser, Photos). It is
// given where to open -- the x and y of the tap -- and its choices, both as
// properties, and it says which was chosen. The card decides it is open.
defineElement<{ open: boolean; x: number; y: number; choices: { value: string; label: string }[]; value: string }>(
    "wos-popup-list",
    { open: Boolean, x: Number, y: Number, choices: Object, value: String },
    ({ open, x, y, choices, value }, { emit, element }) => {
        if (!open) {
            return html``;
        }
        const list = Array.isArray(choices) ? choices : [];
        // Opens at the tap. A first, cheap clamp keeps the corner on screen;
        // then, once it has a size, it is nudged fully into view -- the height
        // depends on how many choices there are, so it cannot be guessed the
        // way a fixed reserve would (a tall menu near the bottom overflowed
        // that). This is what enyo's Popup did with applyAtEventBounds.
        const left = Math.max(4, Number(x) || 0);
        const top = Math.max(4, Number(y) || 0);
        queueMicrotask(() => {
            const menu = element.shadowRoot?.querySelector<HTMLElement>(".wos-popup");
            if (!menu) {
                return;
            }
            const box = menu.getBoundingClientRect();
            const maxLeft = window.innerWidth - box.width - 4;
            const maxTop = window.innerHeight - box.height - 4;
            menu.style.left = `${Math.max(4, Math.min(left, maxLeft))}px`;
            menu.style.top = `${Math.max(4, Math.min(top, maxTop))}px`;
        });
        return html`
            <div class="wos-popup-shade" @click=${() => emit("close")}>
                <div class="wos-popup" style="left: ${left}px; top: ${top}px"
                     @click=${(event: Event) => event.stopPropagation()}>
                    ${list.map((choice) => html`
                        <button class="wos-popup-item ${choice.value === value ? "chosen" : ""}" type="button"
                                @click=${() => emit("choose", { value: choice.value })}>${choice.label}</button>`)}
                </div>
            </div>`;
    },
);

// §25 A section that folds away under its heading: enyo's DividerDrawer, the
// captioned line with an arrow that opens and closes what is under it
// (Preferences, Just Type's providers). The heading is the toggle; it says
// "toggle" with whether it is now meant to be open, and the card decides. What
// the card puts inside is the drawer's content.
defineElement<{ caption: string; open: boolean }>(
    "wos-drawer",
    { caption: String, open: Boolean },
    ({ caption, open }, { emit }) => html`
        <div class="wos-drawer ${open ? "open" : "closed"}">
            <button class="wos-drawer-head" type="button"
                    aria-expanded=${open ? "true" : "false"}
                    @click=${() => emit("toggle", { open: !open })}>
                <span class="wos-drawer-arrow ${open ? "open" : ""}" aria-hidden="true"></span>
                <span class="wos-drawer-caption">${caption}</span>
            </button>
            <div class="wos-drawer-body" ?hidden=${!open}><slot></slot></div>
        </div>`,
);

// §26 A message that slides in over the card and goes away: enyo's Toaster,
// what a card shows when it saves. `from` is the edge it flies in from
// (bottom by default, as HP's were). enyo's Toaster had no timer of its own --
// the card said when to close it -- so this keeps that: it is shown while
// `open`, and it says "dismiss" when tapped or when dragged off, and the card
// decides. A card that wants it to time out sets a timer and clears `open`.
defineElement<{ open: boolean; message: string; from: string }>(
    "wos-toaster",
    { open: Boolean, message: String, from: String },
    ({ open, message, from }, { emit }) => {
        if (!open) {
            return html``;
        }
        const edge = ["bottom", "top", "left", "right"].includes(from) ? from : "bottom";
        return html`
            <div class="wos-toaster from-${edge}" role="status"
                 @click=${() => emit("dismiss")}>
                <span class="wos-toaster-message">${message}</span>
            </div>`;
    },
);

// §27 Two panes, a list and what is chosen in it, side by side when there is
// room and stacked when there is not: enyo's SlidingPane (Email, Contacts,
// Settings on a wide screen). It shows the list slot always; it shows the
// detail slot beside it when the pane is wide and over it when it is not, where
// a back button returns to the list. `showing` is which one is up on a narrow
// screen; it says "back" when the detail's back is tapped.
//
// The pivot is enyo's own multiViewMinWidth, 500px, measured against the
// pane's own width rather than the screen's (kit.css, container query), so a
// card can put it in a column on a wide screen and it still stacks.
defineElement<{ showing: string }>(
    "wos-sliding-pane",
    { showing: String },
    ({ showing }, { emit }) => {
        const onDetail = showing === "detail";
        return html`
            <div class="wos-sliding-pane ${onDetail ? "on-detail" : "on-list"}">
                <div class="wos-pane-list"><slot name="list"></slot></div>
                <div class="wos-pane-detail">
                    <button class="wos-pane-back" type="button"
                            @click=${() => emit("back")}>&#9664;</button>
                    <slot name="detail"></slot>
                </div>
            </div>`;
    },
);
