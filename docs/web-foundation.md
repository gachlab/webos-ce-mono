# The foundation the rewritten cards stand on (#37)

HP's cards are enyo 1.0 from 2011. The rewrites (#36) keep HP's look and flow
but run on today's web platform, inside the same WebAppMgr and its current
Chromium. This is what they stand on, and why it is shaped this way.

## The decision

**No UI framework in the cards. The logic is a plain TypeScript library, and
the controls are custom elements drawn with `lit-html`.**

Two layers, one rule between them: **`src/lib/` never imports anything from the
UI**, and the UI holds no business logic.

| | |
|---|---|
| Logic | `components/cards/src/lib` — services as factory functions with their own state, and one callback to subscribe to it |
| UI | `components/cards/src/ui` — custom elements written as functions, one stylesheet, one card entry point per app |

### Why not a framework

The question was not which framework is nicest to write. It was what happens in
five years, when this port is still here and the framework of the day is not.
enyo answers that question by example: HP's cards cannot be maintained today
without maintaining a 2011 framework alongside them.

* What a card publishes is `<hp-toggle on></hp-toggle>` — an element, not a
  component of anyone's framework. A card written in React, or in nothing at
  all, uses the same controls and the same services, and none of them can tell
  what drew them.
* `lit-html` is the only UI dependency (3 KB), and it is confined to
  `src/ui/element.ts`. Its templates are ordinary JavaScript template literals,
  so there is no compiler in the way and what runs in the card is what was
  written. Replacing it means rewriting that one file.
* The logic never knew about any of this. `src/lib` is tested with
  `node --test`, without a browser, and would survive the UI being thrown away.

Solid was the other candidate, and the nearest miss: fine-grained reactivity,
7 KB, JSX. It was turned down on the same question — a Solid component is a
function only Solid can call, so the kit and every card's view would be rewritten
if it ever went. The parts of it that are worth having are cheap to keep without
it: the state primitive (`create-state.ts`, 40 lines) and a repaint on change.

React with Enact (LG's webOS framework) brings LG's look, not HP's, and React
with it. htmx answers a different problem: it swaps HTML that a server rendered,
and a card has no server -- its data arrives as JSON over the luna bus, much of
it pushed.

### What it costs

* No JSX: control flow is `map`, ternaries and `lit-html`'s `repeat`. Plain
  JavaScript, more verbose than `<For>`.
* A repaint re-runs the template function and compares its holes, rather than
  updating one bound node. At the size of a card -- a list of networks, a screen
  of settings -- it does not show.
* Custom elements are classes; the web platform defines them that way.
  `defineElement` is the one class in the tree, and everything written on top of
  it is a function.

## How a card is put together

```
components/cards/
  src/lib/helpers/      create-state, event-bus, with-deadline
  src/lib/infra/luna/   the bus: the port, the PalmServiceBridge adapter, a fake
  src/lib/services/     one service per screen, a state machine each
  src/ui/element.ts     defineElement: functions in, custom elements out
  src/ui/kit/           HP's controls
  src/ui/hp.css         the look, in numbered sections
  src/cards/<app id>/   index.html, appinfo.json, main.ts -- where it is wired up
```

* **State names are the contract**: `"template:loading"`, `"template:ready"`,
  `"template:failed"`. The UI switches on them; tests assert the sequence.
* **What a service needs arrives in `deps`**. There is no container and no
  import of a concrete thing: `main.ts` is the only file that decides that the
  bus is `PalmServiceBridge` here and a fake in a browser.
* **Every wait is bounded.** A call that is not answered fails with
  `LunaTimeout` rather than leaving a spinner on screen for good.
* **A failure is shown, not swallowed.** What the user reads is what the service
  said; the uri and the rest go to the log.

## Porting HP's cards, rather than rewriting them whole

Nothing here stops an enyo shim being written on top, and that is deliberate:
it is how an HP card can be moved across a screen at a time instead of in one
jump.

* The bus port (`LunaService`) is not tied to our cards. `enyo.PalmService`'s
  `call`/`subscribe`/`cancel` is a thin wrapper over it, and an HP app whose
  service code goes through that wrapper keeps working while its screens are
  replaced.
* The controls are custom elements, so enyo's own DOM-driven code can create
  and use them (`document.createElement("hp-toggle")`) without knowing what
  they are. The mapping that matters is per control, and only the controls an
  app actually uses have to be covered.
* The card lifecycle (`AppService`) is the same one enyo's `ApplicationEvents`
  and `enyo.windows` are built on -- `Mojo.stageActivated`, `relaunch`,
  `PalmSystem.launchParams` -- so both can be in a page at once: the adapter
  keeps whatever hooks were already there rather than replacing them.

The shim itself is its own ticket; what this ticket owes it is that none of the
three above are ever reached through a card's own code.

## Building and testing

* `tools/build-cards.sh` bundles each card with esbuild into `build/cards/<id>/`
  (one `main.js`, the page, the stylesheet, `appinfo.json`);
  `tools/assemble-rootfs.sh` installs those as web apps. `tools/build.sh cards`
  is the stage that runs it, so CI builds them too.
* `components/cards/test/run.sh` type-checks with TypeScript 7 and runs the
  library's tests on `node --test` -- no browser, no bus.
* `tests/template-card.cpp` runs the built card in the engine WebAppMgr uses,
  against a fake `PalmServiceBridge`: it asks the bus, draws HP's rows, says so
  when a service is not running, and asks again when the user presses.
* Everything was checked by mutation.

The sizes, for the record: the template card is 36 KB of JavaScript (10.6 KB
gzipped) including `lit-html` and its own logic, plus 5 KB of CSS. enyo's core
alone is 388 KB of JavaScript and 60 KB of CSS, before the card's own code.
