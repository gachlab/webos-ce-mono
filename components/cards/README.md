cards
=====

webOS cards on the modern web platform: a library that knows no framework, HP's
controls as custom elements, and one card per app.

**Ours, not HP's.** HP's cards are enyo 1.0; these are written from scratch,
with HP's look and flow as the specification. Why it is shaped this way, and
what was turned down, is in `docs/web-foundation.md`.

Layers
------

```
src/lib/      no import from src/ui, ever. Tested with node --test.
src/ui/       custom elements, one stylesheet.
src/cards/    one directory per app: index.html, appinfo.json, main.ts.
```

* `lib/helpers/create-state.ts` — every service's state: `{name, data, error}`,
  and a subscribe that hands the current state over at once, so a card paints
  its first frame without waiting.
* `lib/helpers/event-bus.ts` — what one service says to another. Services never
  import each other.
* `lib/helpers/with-deadline.ts` — every wait is bounded, and the bound covers
  the whole operation.
* `lib/infra/luna/service.ts` — the bus as a card sees it: `call` and
  `subscribe`, `LunaCallError`, `LunaTimeout`. `bridge.service.ts` is
  WebAppMgr's `PalmServiceBridge`; `fake.service.ts` is what tests and a plain
  browser talk to.
* `lib/infra/app/service.ts` — the card's own life: what it was launched with,
  being brought to the front and sent away, relaunched with new parameters, the
  keyboard taking half the screen, the back gesture, and telling WebAppMgr the
  card is ready. `palm-system.service.ts` is that on `window.Mojo` and
  `PalmSystem`, keeping whatever hooks were already on the page.
* `lib/i18n/translate.ts` — `t("Turn on Wi-Fi")`. The key is the English text,
  so a missing translation shows English rather than a dotted name.
* `lib/services/<screen>.service.ts` — one screen, one state machine, made by a
  factory function that takes what it needs in `deps`.
* `ui/element.ts` — `defineElement(name, props, component)`. Components are
  functions; what comes out is an ordinary custom element with its own shadow
  root, adopting the one stylesheet. The only class in the tree is in here.
* `ui/kit/kit.ts` — HP's controls: `hp-header`, `hp-button`, `hp-toggle`,
  `hp-row`, `hp-group`, `hp-spinner`, `hp-field`, `hp-check`, `hp-selector`
  (the row that opens a drawer of choices), `hp-dialog` and `hp-progress`.
* `ui/hp.css` — the look, in numbered sections. A new class goes in its section
  and nowhere else.

Writing a card
--------------

```ts
// lib/services/wifi.service.ts -- the logic, with no idea what draws it
export const createWifiService = (deps: { luna: LunaService }): WifiService => { ... };

// cards/com.palm.app.wifi/main.ts -- where it is wired up
const service = createWifiService({ luna: openBus() });
service.onStateChange((state) => render(view(state, service), root));
```

The view is a function of the state, and the controls answer with events:

```ts
html`<hp-toggle ?on=${state.data.on} @toggle=${(e) => service.onToggle(e.detail.on)}></hp-toggle>`
```

Anything an attribute cannot carry (a list, an object) is set as a property,
with `setProperties`.

Building and running
--------------------

```sh
tools/build-cards.sh                 # all of them, into build/cards/<app id>/
tools/build-cards.sh com.palm.app.template
tools/assemble-rootfs.sh             # installs them as web apps
components/cards/test/run.sh         # type check (TypeScript 7) and the tests
```

A card also opens in an ordinary browser: with no `PalmServiceBridge` on the
page, `main.ts` hands the service a fake bus instead, so a screen can be written
and looked at without a session.

The two cards that come with it
-------------------------------

`src/cards/com.palm.app.kit` is the showcase: every control, in each state it
can be in, and a list of what WebAppMgr has said to the card so far. It is the
kit itself, so it cannot go stale, and it is where a change to `hp.css` is
looked at before it reaches a card.

`src/cards/com.palm.app.template` is the one the others are copied from. It asks
`com.palm.deviceprofile` who this device is, shows it in HP's rows, says so when
the service is not running, and asks again when the user presses. It is what
`tests/template-card.cpp` drives in the engine WebAppMgr uses.
