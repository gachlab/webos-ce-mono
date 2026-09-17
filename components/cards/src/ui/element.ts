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
// stylesheet: `useStyles` hands kit.css to every element at once, and each
// root adopts that one sheet rather than carrying a copy.

import { render, type TemplateResult } from "lit-html";

export type Attributes = Record<string, unknown>;

// How an attribute's text becomes a property. `Boolean` is the HTML kind:
// present is true, absent is false. `Object` is what an attribute cannot
// carry -- a list, a record -- and is only ever set as a property.
export type Reader = typeof String | typeof Number | typeof Boolean | typeof Object;

export interface Host {
    // Sends a DOM event, which is how a component answers its card: composed
    // and bubbling, like the platform's own.
    emit(type: string, detail?: unknown): void;
    // Runs when the element leaves the document. Only what is registered on
    // the first paint is kept, so a component that subscribes has to ask
    // whether this is that paint.
    onRemoved(cleanup: () => void): void;
    // True while the element is being drawn for the first time: where a
    // component sets up whatever it has to tear down later.
    firstPaint(): boolean;
    readonly element: HTMLElement;
}

export type Component<Props extends Attributes> = (props: Props, host: Host) => TemplateResult;

// The stylesheet every element adopts. A card calls this once, with kit.css;
// startCard does it.
//
// The sheet is made now and filled in then: an element that was drawn before
// the card got round to calling this -- one the page built itself, before the
// bundle ran -- has already adopted this very object, and filling it in styles
// it too. A sheet handed over per element would have left those unstyled.
const sheet: CSSStyleSheet | undefined = (() => {
    try {
        return new CSSStyleSheet();
    } catch {
        // No constructable stylesheets: each root gets a <style> of its own.
        return undefined;
    }
})();
const fallbacks = new Set<HTMLStyleElement>();
let styleText = "";

export const useStyles = (css: string): void => {
    styleText = css;
    sheet?.replaceSync(css);
    for (const style of fallbacks) {
        style.textContent = css;
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
    if (reader === Object) {
        return undefined;
    }
    if (value === null) {
        return reader === Number ? 0 : "";
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

    const Element = class extends HTMLElement {
        static readonly observedAttributes = attributes;

        // Set by the card as a property (element.networks = [...]) for
        // anything an attribute cannot carry.
        readonly #values: Attributes = {};
        readonly #cleanups: (() => void)[] = [];
        #connected = false;
        #painting = false;
        #dirty = false;
        #painted = false;
        #root: ShadowRoot | undefined;
        #hostApi: Host | undefined;

        connectedCallback(): void {
            this.#connected = true;
            // A value set before the definition arrived sits on the element as
            // an own property, where the accessor below cannot see it. This is
            // the upgrade the platform expects a custom element to do: take it,
            // remove it, and set it through the accessor.
            for (const key of properties) {
                if (Object.hasOwn(this, key)) {
                    const value = (this as unknown as Attributes)[key];
                    delete (this as unknown as Attributes)[key];
                    this.setProperty(key, value);
                }
            }
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

        // What the accessors below are written in terms of, and what a caller
        // with a name in a variable uses.
        setProperty(key: string, value: unknown): void {
            this.#values[key] = value;
            this.#paint();
        }

        // What the component would be handed for this property right now.
        readProperty(key: string): unknown {
            const fromProperty = this.#values[key];
            return fromProperty !== undefined
                ? fromProperty
                : read(this.getAttribute(attributeName(key)), props[key as keyof Props]);
        }

        #paint(): void {
            if (!this.#connected) {
                return;
            }
            // A repaint asked for while painting -- a component that sets a
            // property on itself -- is not a loop and is not lost either: it
            // happens once, after this one.
            if (this.#painting) {
                this.#dirty = true;
                return;
            }
            this.#painting = true;
            try {
                const current: Attributes = {};
                for (const key of properties) {
                    current[key] = this.readProperty(key);
                }
                render(component(current as Props, this.#host()), this.#shadow());
                this.#painted = true;
            } finally {
                this.#painting = false;
            }
            if (this.#dirty) {
                this.#dirty = false;
                this.#paint();
            }
        }

        #shadow(): ShadowRoot {
            if (!this.#root) {
                this.#root = this.attachShadow({ mode: "open" });
                if (sheet) {
                    this.#root.adoptedStyleSheets = [sheet];
                } else {
                    const style = document.createElement("style");
                    style.textContent = styleText;
                    this.#root.append(style);
                    fallbacks.add(style);
                }
            }
            return this.#root;
        }

        // One host per element, not one per paint: a component that subscribes
        // and registers a cleanup would otherwise leave one subscription and
        // one cleanup behind on every repaint.
        #host(): Host {
            if (!this.#hostApi) {
                this.#hostApi = {
                    element: this,
                    emit: (type, detail) =>
                        this.dispatchEvent(new CustomEvent(type, { detail, bubbles: true, composed: true })),
                    onRemoved: (cleanup) => {
                        if (!this.#painted) {
                            this.#cleanups.push(cleanup);
                        }
                    },
                    firstPaint: () => !this.#painted,
                };
            }
            return this.#hostApi;
        }
    };

    // Each declared property is a real property of the element: `el.choices =
    // [...]` repaints, which is what every other way of driving an element --
    // lit-html's `.prop=`, a framework's binding, enyo's own DOM code -- does.
    for (const key of properties) {
        Object.defineProperty(Element.prototype, key, {
            configurable: true,
            enumerable: true,
            get(this: InstanceType<typeof Element>) {
                return this.readProperty(key);
            },
            set(this: InstanceType<typeof Element>, value: unknown) {
                this.setProperty(key, value);
            },
        });
    }

    customElements.define(name, Element);
};

// Several properties at once, by name. Plain assignment (`el.choices = [...]`,
// or lit-html's `.choices=${...}`) does the same thing: the element's declared
// properties are real properties, and one set before the definition arrived is
// taken at upgrade.
export const setProperties = (element: Element, values: Attributes): void => {
    Object.assign(element, values);
};

export { html, nothing, render, type TemplateResult } from "lit-html";
export { repeat } from "lit-html/directives/repeat.js";
export { classMap } from "lit-html/directives/class-map.js";
