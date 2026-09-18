@webos/ui-kit
=============

The system's look: HP's controls as custom elements, one stylesheet, and the
two themes. **Ours, not HP's** — these are written from scratch with HP's look
and flow as the specification. Why it is shaped this way, and what was turned
down, is in `docs/web-foundation.md`.

```
src/element.ts      defineElement(name, props, component)
src/kit/kit.ts      the controls
src/kit.css         their look, in numbered sections
src/page.css        the page around them
src/theme-*.css     the same token names, twice
src/start-card.ts   how a card starts
src/open-bus.ts     the bridge on a device, a fake in a browser
showcase/           every control, in each state -- an app of its own
```

* `element.ts` — components are functions; what comes out is an ordinary custom
  element with its own shadow root, adopting the one stylesheet. The only class
  in the tree is in here.
* `kit/kit.ts` — `hp-header`, `hp-button`, `hp-toggle`, `hp-row`, `hp-group`,
  `hp-spinner`, `hp-field`, `hp-check`, `hp-selector` (the row that opens a
  drawer of choices), `hp-choice` (one of a few, in the row itself),
  `hp-dialog`, `hp-progress`, `hp-swipe-row` (swipe to delete, with HP's inline
  confirmation), `hp-app-menu`, `hp-activity-button` (the one that shows it is
  working) and `hp-slider`. What is still missing is #58.
* `start-card.ts` — the stylesheet, the first frame, telling WebAppMgr the card
  is ready, and giving the service the card's own life (shown, hidden,
  relaunched, back).
* `kit.css` reads every colour from a token; `theme-enyo.css` sets those tokens
  to what enyo's Onyx theme measures, and `theme-modern.css` to colours that
  make sense now. Same 69 names, either file. A new class goes in its section
  and nowhere else.

Writing a card
--------------

```ts
// the logic, with no idea what draws it
export const createWifiService = (deps: { luna: LunaService }): WifiService => { ... };

// main.ts -- where it is wired up
startCard({
    service: createWifiService({ luna: openBus() }),
    view: (state, service) => html`...`,
});
```

The view is a function of the state, and the controls answer with events:

```ts
html`<hp-toggle ?on=${state.data.on} @toggle=${(e) => service.onToggle(e.detail.on)}></hp-toggle>`
```

Anything an attribute cannot carry is set as a property -- `.choices=${list}`
from a template, `element.choices = list` from anywhere else. A property set
before the element's definition arrived is taken when it does.

The showcase
------------

`showcase/` is an app like any other (`com.gachlab.app.kit`): every control, in
each state it can be in, and a list of what WebAppMgr has said to the card so
far. It lives **inside this package** on purpose — it is the kit's
documentation, and documentation that ships somewhere else goes stale. It is
where a change to `kit.css` is looked at before it reaches a card, and its
other half is `apps/reference/kit-enyo`, the same controls drawn by enyo, so
"does it look the same" is a measurement instead of an argument.
