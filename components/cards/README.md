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
  its first frame without waiting. An error belongs to the state it happened
  in: a new name leaves it behind.
* `lib/helpers/timers.ts` — the clock, as something a test can hand over.
* `lib/helpers/watch.ts` — a subscription that follows the card: on while it is
  being looked at, off while it is not. A card that is sent away keeps its
  page; what it must not keep is a service pushing at it.
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
* `lib/infra/luna/<service>.ts` — one webOS service, typed: its uris, its reply
  shapes and its quirks, in one place. `connectionmanager.ts` is the first.
  A card's service asks these, never the bus directly.
* `lib/services/navigation.service.ts` — which screen a card is showing.
  HP's cards are a stack: a row opens a screen, back pops it, and the card
  closes when there is nothing left to pop.
* `lib/services/<screen>.service.ts` — one screen, one state machine, made by a
  factory function that takes what it needs in `deps`.
* `ui/element.ts` — `defineElement(name, props, component)`. Components are
  functions; what comes out is an ordinary custom element with its own shadow
  root, adopting the one stylesheet. The only class in the tree is in here.
* `ui/kit/kit.ts` — HP's controls: `hp-header`, `hp-button`, `hp-toggle`,
  `hp-row`, `hp-group`, `hp-spinner`, `hp-field`, `hp-check`, `hp-selector`
  (the row that opens a drawer of choices), `hp-dialog` and `hp-progress`.
* `ui/start-card.ts` — how a card starts: the stylesheet, the first frame,
  telling WebAppMgr the card is ready, and giving the service the card's own
  life (shown, hidden, relaunched, back).
* `ui/kit.css` — the controls' look, in numbered sections; every element adopts
  this one sheet. `ui/page.css` is the page around them. A new class goes in
  its section and nowhere else.

Writing a card
--------------

```ts
// lib/services/wifi.service.ts -- the logic, with no idea what draws it
export const createWifiService = (deps: { luna: LunaService }): WifiService => { ... };

// cards/com.palm.app.wifi/main.ts -- where it is wired up
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
kit itself, so it cannot go stale, and it is where a change to `kit.css` is
looked at before it reaches a card.

`src/cards/com.palm.app.template` is the one the others are copied from, and it
does everything a card does: it asks `com.palm.deviceprofile` who this device
is, watches `com.palm.connectionmanager` while it is on screen and stops while
it is not, shows what came back in HP's rows, says so when a service is not
running, asks again when the user presses, and opens a second screen that the
back gesture comes out of before the card closes. It is what
`tests/template-card.cpp` drives in the engine WebAppMgr uses.
