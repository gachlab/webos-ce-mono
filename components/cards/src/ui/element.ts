// Components written as functions; custom elements out the other side.
//
// The web platform defines custom elements as classes, and this is the one
// place in the tree that has one. Everything above is a function:
//
//     defineElement("hp-toggle", { on: Boolean }, ({ on }, { emit }) => html`
//         <button class="hp-toggle ${on ? "on" : "off"}"
//                 @click=${() => emit("change", { on: !on })}></button>`);
//
// What comes out is an ordinary element: <hp-toggle on></hp-toggle> works from
// a card, from plain HTML, and from any framework, and none of them knows what
// drew it. If lit-html is ever replaced, it is replaced here.
//
// Each element draws into its own shadow root, so what a card puts inside it
// (<hp-group><hp-row>...) is still there after a repaint, and a card's own CSS
// cannot reach in and change what a control looks like. HP's look is still one
// stylesheet: `useStyles` hands hp.css to every element at once, and each root
// adopts that one sheet rather than carrying a copy.

import { render, type TemplateResult } from "lit-html";

export type Attributes = Record<string, unknown>;

// How an attribute's text becomes a property. `Boolean` is the HTML kind:
// present is true, absent is false.
export type Reader = typeof String | typeof Number | typeof Boolean;

export interface Host {
    // Sends a DOM event, which is how a component answers its card: composed
    // and bubbling, like the platform's own.
    emit(type: string, detail?: unknown): void;
    // Runs when the element leaves the document. Subscriptions go here.
    onRemoved(cleanup: () => void): void;
    readonly element: HTMLElement;
}

export type Component<Props extends Attributes> = (props: Props, host: Host) => TemplateResult;

// The stylesheet every element adopts. A card calls this once, with hp.css.
let styles: CSSStyleSheet | undefined;
let styleText = "";

export const useStyles = (css: string): void => {
    styleText = css;
    try {
        const sheet = new CSSStyleSheet();
        sheet.replaceSync(css);
        styles = sheet;
    } catch {
        // No constructable stylesheets: each root gets a <style> instead.
        styles = undefined;
    }
};

// `labelOn` is written `label-on` in markup: HTML lowercases attribute names,
// so a camelCase one can never be read back.
export const attributeName = (property: string): string =>
    property.replace(/[A-Z]/g, (letter) => `-${letter.toLowerCase()}`);

const read = (value: string | null, reader: Reader): unknown => {
    if (reader === Boolean) {
        return value !== null;
    }
    if (value === null) {
        return undefined;
    }
    return reader === Number ? Number(value) : value;
};

export const defineElement = <Props extends Attributes>(
    name: string,
    props: { [Key in keyof Props]: Reader },
    component: Component<Props>,
): void => {
    if (customElements.get(name)) {
        return;
    }
    const properties = Object.keys(props);
    const attributes = properties.map(attributeName);

    customElements.define(name, class extends HTMLElement {
        static readonly observedAttributes = attributes;

        // Set by the card as a property (element.networks = [...]) for
        // anything an attribute cannot carry.
        readonly #values: Attributes = {};
        readonly #cleanups: (() => void)[] = [];
        #connected = false;
        #painting = false;
        #root: ShadowRoot | undefined;

        connectedCallback(): void {
            this.#connected = true;
            this.#paint();
        }

        disconnectedCallback(): void {
            this.#connected = false;
            for (const cleanup of this.#cleanups.splice(0)) {
                cleanup();
            }
            if (this.#root) {
                render(undefined, this.#root);
            }
        }

        attributeChangedCallback(): void {
            this.#paint();
        }

        // A card sets a property; the element repaints. Declared here rather
        // than on the prototype so `element.networks = [...]` before the
        // element upgrades is not lost.
        setProperty(key: string, value: unknown): void {
            this.#values[key] = value;
            this.#paint();
        }

        #paint(): void {
            // A repaint asked for while painting -- a component that sets a
            // property on itself -- is one repaint, not a loop.
            if (!this.#connected || this.#painting) {
                return;
            }
            this.#painting = true;
            try {
                const current: Attributes = {};
                for (const key of properties) {
                    const fromProperty = this.#values[key];
                    current[key] = fromProperty !== undefined
                        ? fromProperty
                        : read(this.getAttribute(attributeName(key)), props[key as keyof Props]);
                }
                render(component(current as Props, this.#host()), this.#shadow());
            } finally {
                this.#painting = false;
            }
        }

        #shadow(): ShadowRoot {
            if (!this.#root) {
                this.#root = this.attachShadow({ mode: "open" });
                if (styles) {
                    this.#root.adoptedStyleSheets = [styles];
                } else if (styleText) {
                    const style = document.createElement("style");
                    style.textContent = styleText;
                    this.#root.append(style);
                }
            }
            return this.#root;
        }

        #host(): Host {
            return {
                element: this,
                emit: (type, detail) =>
                    this.dispatchEvent(new CustomEvent(type, { detail, bubbles: true, composed: true })),
                onRemoved: (cleanup) => this.#cleanups.push(cleanup),
            };
        }
    });
};

// What a card does to give an element what an attribute cannot carry: a list,
// an object, a function.
export const setProperties = (element: Element, values: Attributes): void => {
    const target = element as Element & { setProperty?: (key: string, value: unknown) => void };
    for (const [key, value] of Object.entries(values)) {
        if (target.setProperty) {
            target.setProperty(key, value);
        } else {
            (target as unknown as Attributes)[key] = value;
        }
    }
};

export { html, nothing, render, type TemplateResult } from "lit-html";
export { repeat } from "lit-html/directives/repeat.js";
export { classMap } from "lit-html/directives/class-map.js";
