# The foundation the rewritten cards stand on (#37)

HP's cards are enyo 1.0 from 2011. The rewrites (#36) keep HP's look and flow
but run on today's web platform, inside the same WebAppMgr and its current
Chromium. This is what they stand on, and why it is shaped this way.

## The decision

**No UI framework in the cards. The logic is a plain TypeScript library, and
the controls are custom elements drawn with `lit-html`.**

Two layers, one rule between them: **`@webos/api` never imports anything from
the UI**, and the UI holds no business logic. It is a package boundary now, and
`tools/test-web.sh` fails when it is crossed.

| | |
|---|---|
| Logic | `sdk/webos-api` — services as factory functions with their own state, and one callback to subscribe to it |
| UI | `sdk/ui-kit` — custom elements written as functions, one stylesheet, one card entry point per app |

### Why not a framework

The question was not which framework is nicest to write. It was what happens in
five years, when this port is still here and the framework of the day is not.
enyo answers that question by example: HP's cards cannot be maintained today
without maintaining a 2011 framework alongside them.

* What a card publishes is `<wos-toggle on></wos-toggle>` — an element, not a
  component of anyone's framework. A card written in React, or in nothing at
  all, uses the same controls and the same services, and none of them can tell
  what drew them.
* `lit-html` is the only UI dependency, and its templates are ordinary
  JavaScript template literals, so there is no compiler in the way and what
  runs in the card is what was written.
* Being honest about how far that goes: the kit and the cards are written in
  lit-html's dialect (`?on=`, `@press=`, `.choices=`), so replacing it means
  rewriting them. What it does *not* reach is the consumers: an element's
  contract is its tag, its attributes, its properties and its events, and a
  card -- or a shim, or another framework -- that uses `<wos-toggle>` cannot
  tell what drew it. That is the part that has to outlive the library, and it
  does.
* The logic never knew about any of this. `sdk/webos-api` is tested with
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

Three packages, not three directories: npm workspaces, so an app reaches the
platform and the kit **by name**, and the layering is resolution rather than
discipline.

```
sdk/webos-api/            @webos/api -- no DOM. Nothing here knows there is a screen.
  src/helpers/            create-state, timers, watch
  src/services/           navigation: which screen the card is showing
  src/infra/luna/         the bus: the port, the PalmServiceBridge adapter, a
                          fake, and one file per webOS service, typed
  src/infra/app/          the card's own life, on window.Mojo and PalmSystem,
                          and connectCard: everything being a card means
                          except what draws

sdk/ui-kit/               @webos/ui-kit -- depends on @webos/api, never the reverse
  src/element.ts          defineElement: functions in, custom elements out
  src/start-card.ts       connectCard, plus a lit-html render. The optional half.
  src/kit/                HP's controls (all of them; #58 finished the set)
  src/kit.css             the controls' look; every element adopts this one sheet
  src/page.css            the page a card lives on, and the text it writes
  src/theme-*.css         the same token names, twice
  showcase/               every control in every state -- an app, and the kit's
                          documentation, which is why it lives in here

apps/<name>/              one package per app
  src/main.ts             where the service and the view are wired up
  src/<screen>.service.ts one screen, one state machine
  src/luna/<service>.ts   a webOS service only this app talks to
  src/appinfo.json        the id; the directory name is not the id
  test/                   the service, with a fake bus
```

* **The direction of the dependency is enforced, not agreed.** `@webos/api`
  must not mention the kit; `tools/test-web.sh` fails if it does. Workspaces put
  every package in `node_modules`, so node would resolve a wrong import happily
  -- that check is what says no. Without it, "a card can be written in something
  that is not our kit" would be a claim rather than a fact (#65).

* **State names are the contract**: `"template:loading"`, `"template:ready"`,
  `"template:failed"`. The UI switches on them; tests assert the sequence.
* **What a service needs arrives in `deps`**. There is no container and no
  import of a concrete thing: `main.ts` is the only file that decides that the
  bus is `PalmServiceBridge` here and a fake in a browser.
* **Every wait is bounded.** A call that is not answered fails with
  `LunaTimeout`, and so does a subscription whose first reply never comes --
  a service that is not there never answers at all, and a card that waits for
  it shows its spinner for good.
* **A failure is shown, not swallowed.** What the user reads is what the service
  said; the uri and the rest go to the log.
* **A card that is not being looked at is not listening.** A subscription is
  started when the card is shown and cancelled when it is sent away
  (`helpers/watch.ts`), which `startCard` wires to the card's own life.
* **Screens are a stack.** A row opens one, the back gesture pops it, and only
  an empty stack closes the card -- HP's flow, written once
  (`sdk/webos-api/src/services/navigation.service.ts`).

### Three layers, and only two of them are compulsory

```
@webos/api     the bus, the card's own life, launch params, translation.
               No DOM. connectCard lives here.
@webos/ui-kit  the controls (custom elements, styled by themselves) and, on
               top, our renderer: defineElement + lit-html + startCard.
an app         picks what it needs of the two.
```

The line that matters is not between the packages, it is **inside `ui-kit`**.
Its controls are ordinary custom elements: `document.createElement("wos-row")`
from React, from an enyo shim (#56), from nothing at all, and the control comes
out looking right — because importing the kit is what styles it, at definition
time. Our renderer is 316 lines (`element.ts` 252 + `start-card.ts` 64) and
it is **optional by construction**.

`startCard` is `connectCard` plus one line that renders a lit-html template.
Everything that makes something a card on this device — the lifecycle,
`stageReady`, the back gesture, letting go when the page unloads — is
`connectCard`, in the package with no DOM in it.

**enyo's mistake was not having layers, it was making the top one compulsory.**
That is why porting one of HP's cards today means rewriting it, and it is the
single thing this foundation exists not to repeat. So the claim gets a file:
`apps/example-plain` is a card written with none of our renderer, and
`tests/plain-card.cpp` fails the day it stops working.

### Measured: the kit from five frameworks

The claim above -- that a card could be written in something that is not our
renderer -- was an inference until it was measured. It has been, on
2026-09-18, against the kit exactly as it ships.

The probe is `wos-selector`, chosen because it is the hardest case in the kit:
its `choices` is an **array**, which no attribute can carry, and it answers with
custom events. Each framework renders one, and three questions are asked of the
result: did the property arrive (the control shows "WPA Personal" rather than
falling back to "wpa"), is it styled (2.6rem of page.css's 20px root = the 52px
HP's rows were), and does the framework hear the event and repaint.

| | property | styled | event | what it costs |
|---|---|---|---|---|
| React 19.3 | yes | 52px | yes | a `ref` and `addEventListener`: JSX has no mapping for custom events |
| Vue 3.5 | yes | 52px | yes | **nothing** |
| Solid 1.9 | yes | 52px | yes | `prop:` and `on:`, both first-class |
| Svelte 5.57 | yes | 52px | yes | **nothing** |
| Angular 22.1 | yes | 52px | yes | `CUSTOM_ELEMENTS_SCHEMA`, then `[prop]` and `(event)` natively |

The probe was checked against itself first: with `choices` not passed it reads
`"wpa"`, and with the listeners removed the drawer never opens. It is not a
tautology.

**The one thing that does not travel: property-only data.** React **18**
stringifies the array into an attribute --
`choices="[object Object],[object Object],[object Object]"` -- so the property
never arrives and the control silently shows its fallback. No error, just the
wrong content. React 19 fixed it.

That is worth knowing beyond React 18, because **anything that can only write
markup has the same problem**: HTML by hand, `innerHTML`, a page rendered on a
server, and the enyo shim of #56, which builds its DOM from JavaScript objects.
Attributes and custom events travel everywhere; an array does not.

So the controls whose data is a list -- `wos-selector`'s and `wos-choice`'s
`choices`, `wos-app-menu`'s `items` -- read that list from a JSON attribute too
(#70):

```html
<wos-selector label="Security" value="wpa"
              choices='[{"value":"wpa","label":"WPA Personal"}]'></wos-selector>
```

**The two are not equivalent, and the property is the one to reach for.** A
card, or any code with an object in hand, sets the property (`el.choices =
[...]`, lit-html's `.choices=`, a framework's binding): it carries the real
value, there is nothing to serialise, and nothing to mis-serialise. The JSON
attribute is for the writer who has only markup and no property to set. When
both are there the property wins -- the attribute is the fallback, not a second
source of truth. A malformed attribute is not a new failure: it is read only
when it is present, and text that does not parse is dropped in silence, so the
control falls back exactly as it would with no attribute at all rather than
taking the page down.

What this did **not** cover, and it should be said: one control, one
interaction, in headless Chrome rather than on the device, and the harness
(five frameworks and their `node_modules`, which needs the network) is not in
the repository, so this is a measurement taken once and written down -- not a
test that runs in CI. `apps/example-plain` is the part that does run, on every
build.

### The names we publish are `wos-`

The controls used to be `hp-toggle`, `hp-row`, `--hp-accent`. That prefix was
wrong twice over: it is someone else's brand, and it is a **false claim about
who wrote the code** -- these are ours, reimplementing a look. It would also
collide the day a real piece of HP's markup and ours meet on one page.

They are `wos-` now, all 71 tokens and every element and class with them. Not
`gach-` and not `gl-`: an app's **id** says who publishes it
(`com.gachlab.app.wifi`), but an **element** says which system it belongs to,
and somebody else writing a card for this device should be reaching for "the
system's toggle", not for ours.

`theme-enyo.css` keeps its name. There the word is exact: it is enyo's look,
measured.

## What a rewritten card is allowed to look like

**A reimplementation keeps the look of the card it replaces.** New code,
new stylesheet, same screen: the rows sit where they sat, at the size they sat
at, in the colours they were. Nothing here says the 2011 look is the right one
forever -- it says that a rewrite is not the place to decide that, because
"is this better?" and "did this break?" cannot be answered in the same
screenshot. When a card should get a new layout, that is its own ticket.

So the kit is a reimplementation of enyo's Onyx theme in modern CSS. Not an
impression of it: the values come from enyo's own stylesheets in this tree
(`components/enyo-1.0/framework/source/palm/themes/Onyx/css`) and, where the
theme drew with images, from the images themselves -- `button-up.png`,
`toggle-button.png`, `checkbox.png`, `radiobutton.png`, `progress-bar.png`,
`slider-track.png` read pixel by pixel. That is how we found that HP's toggle
is blue and not green, that its slider never fills, and that its checkbox is
the toggle's blue with a pale tick rather than a white one on green.

Two consequences worth knowing:

* **The root font size is enyo's 20px** (`page.css` sets `125%`). enyo wrote
  its controls in rem over that root -- `.enyo-item` is `.9rem`, a group's
  caption `.7rem` -- so on the same root its numbers transfer unchanged. Every
  size in the kit is in rem for the same reason, and one root moves all of
  them. The only lengths left in px are hairlines: a separator is one line, not
  a measure.
* **The layout is modern even where the look is not.** Grid for the header's
  three columns, flexbox for rows, `transform` for the toggle's knob and the
  slider's -- not the absolute positioning enyo needed. What it looks like is
  2011; how it is written is not.

### Two themes

Every colour in the kit comes from a custom property, and the properties are
defined in one file:

* `theme-enyo.css` -- HP's values, as described above. The default: a card
  links it and looks like the card it replaced.
* `theme-modern.css` -- the same property names with values chosen for a screen
  somebody is looking at today: lists on white, one accent colour, no sheen on
  anything, softer separators.

A card links one of them before `page.css` and changes nothing else. The kit
showcase (`com.gachlab.app.kit`) switches between them at the top of the card,
which is also the fastest way to see what a token actually paints. A card that
links neither has no colours at all, and `tests/template-card.cpp` fails on it.

### The other side of the A/B

`apps/baseline/kit-enyo` is the same kit built out of enyo 1.0 and the Onyx theme,
installed as `com.gachlab.app.kitenyo`. It is not a card anybody uses: it is the
reference. Same controls, same order, same captions as `com.gachlab.app.kit`, so
the two can be photographed at the same scroll offset and compared pixel by
pixel rather than argued about. Every difference listed above was found that
way, after the rewritten card had already been called finished by eye.

#### Comparing the computed styles, not just the pixels

A screenshot tells you two controls look different; it does not tell you *why*,
and it lies when the difference is a few pixels of padding or a line drawn one
way versus another. The faster, exact tool is to read `getComputedStyle` and
`getBoundingClientRect` off the same element in both cards over the Chrome
DevTools Protocol (the webOS session exposes an inspector on port 9222; the two
targets are titled `Kit` and `Kit (enyo)`), and diff the numbers.

This is how #58's real bugs were found after the eye had signed off:

* **A note glued under a list.** The grey paragraph under a captioned group sat
  `-12px` into the last row. The cause was not the note: the captioned list
  carried `margin: -12px` on all four sides (to pull it inside
  `group-labeled.png`), and the `-12px` *bottom* dragged the next sibling up.
  `getBoundingClientRect` gave `noteTop 135 < listBottom 147` at once; no
  screenshot would have named the bottom margin.
* **A rule drawn across a divider caption.** The kit painted a grey
  `linear-gradient` line through the middle of the `Nearby` caption. Reading
  enyo's `.enyo-divider-caption` showed `background: none, border: none` — enyo
  draws the captioned divider as a plain item with only the row's bottom
  hairline, and puts a *blue* rule (`divider.png`) on the AlphaDivider alone.
  The comparison, not the eye, said which divider carries a line.
* **A picker pill that looked too short.** The pill measured 32px against
  enyo's 52px — until the computed style showed enyo's 52 was `content 32px +
  border-image 10px top and bottom` (the transparent slices of
  `picker-pill.png`). The visible pill matched; there was nothing to fix.

Notes on doing it:

* Some controls paint with `border-image` (buttons, picker pill, the light
  toolbar). `getComputedStyle` returns the *token*, not the colour, so a colour
  read comes back empty. For those, screenshot and sample the PNG:
  `convert IMG -format '%[pixel:p{x,y}]' info:`. That is also how the numbers in
  `divider.png` (a 4px blue rule with a white highlight) were read.
* `getComputedStyle` works on off-screen nodes; `getBoundingClientRect` gives
  viewport-relative coordinates that are negative or clipped for them. enyo's
  Scroller does not honour the DOM's `scrollIntoView`, so a control low in the
  reference card cannot be screenshotted — but its computed styles still read,
  which is often all the comparison needs.
* Open the CDP websocket with `create_connection(url, suppress_origin=True)`;
  QtWebEngine drops the connection otherwise.
* Prove a fix on the live page first by injecting a `<style>` over the offending
  rule and re-measuring, before touching the CSS and running the build. It turns
  a build-deploy-reload loop into a one-shot check.

This is packaged as `tools/kit-ab.py`: name a control and it reads the box and
type off both cards and prints them side by side with the differences flagged.

    tools/kit-ab.py --list
    tools/kit-ab.py divider-caption row-detail picker-pill

A control is a row in its `CONTROLS` table (a path into the kit's shadow DOM and
the matching Onyx selector); add one when a new bug needs measuring. Ad-hoc
probes from #58 also live in `scratchpad/` (`cdp-ab-detail.py`,
`cdp-divider-deep.py`, `cdp-enyo-twoline.py`, `cdp-probe-dom.py`, and
`cdp-try-fix.py` for the live-override check) as worked examples.

### What the kit covers, and what it does not

The kit reimplements the controls HP's cards are built out of, read from enyo's
own kinds (`components/enyo-1.0/framework/source/palm/`) and its Onyx theme. As
of #58 it covers the set the cards need:

* **Structure and text.** `wos-header` (with HP's light settings toolbar and
  the back arrow), `wos-group`, `wos-row`, `wos-divider` (captioned, and the
  sticky-letter `alpha` kind for a long list), and the card's own `note` and
  `error` text.
* **Buttons.** `wos-button` (enyo's kinds: plain, dark, affirmative, negative,
  blue, gray), `wos-activity-button` (turns into a spinner while it works),
  `wos-icon-button` (the round picture button in a header or toolbar), and
  `wos-tab-group` (the tabs across the top of a card).
* **Input.** `wos-field`, `wos-search-field` (magnifier, then a clear cross
  once something is typed), `wos-text-area` (grows with what is typed, then
  scrolls), `wos-check`, `wos-toggle`, `wos-slider`, `wos-picker` (a wheel of
  values; a date or time picker is three of these side by side).
* **Choosing.** `wos-choice` (one of a few, in the row), `wos-selector` (the
  drawer of choices under a row), and `wos-popup-list` (the list that opens
  where it was tapped, clamped to the viewport).
* **Lists that scale.** `wos-swipe-row` (swipe to delete, with HP's inline
  confirmation), and `wos-list`, which draws only the window of rows on screen
  so a list of thousands costs the rows that are visible.
* **Over the card.** `wos-dialog` (modal, with a focus trap and Escape to
  dismiss), `wos-app-menu`, `wos-toaster` (slides in from an edge; the card
  decides when it goes, as enyo's did), `wos-drawer` (a section that folds away
  under its heading), and `wos-sliding-pane` (list and detail side by side when
  there is room, one at a time with a back arrow when there is not).
* **Waiting.** `wos-spinner`, `wos-progress`, `wos-info`.
* **The keyboard.** `startCard` reserves room for the virtual keyboard when
  WebAppMgr reports it (`--wos-keyboard` in `page.css`) and scrolls the focused
  field clear of it, so a field at the bottom of a card is not typed into from
  behind the keyboard.

What it **deliberately does not** cover:

* **enyo itself** -- its kinds system, its layout engine, its hundred controls.
  The target is HP's *look*, on the controls a card actually uses, not a port
  of the framework.
* **A row control reused per index (enyo's flyweight).** `wos-list` keeps the
  visible window in the DOM instead. A framework that owns its own DOM cannot
  assume one control is re-populated per row, and the window costs the same.
* **A control's own state.** Every control reports what the user asked for with
  an event and lets the card decide what becomes true, because on a device the
  answer comes from a service. A toggle does not turn itself on.
* **JSON-by-attribute for the list-valued controls.** `choices`, `items`,
  `tabs`, `rows` are set as properties, not attributes; whether they should
  also parse a JSON attribute is #70.

Anything a future card needs that is not here is added the same way: read the
enyo control for its behaviour, put it on screen next to `kit-enyo` for its
look, write it as a function in `src/kit/`, give its colours a token in **both**
themes, add it to the showcase in each of its states, and test it by mutation
in `tests/kit-elements.cpp`.

## Porting HP's cards, rather than rewriting them whole

Nothing here stops an enyo shim being written on top, and that is deliberate:
it is how an HP card can be moved across a screen at a time instead of in one
jump.

* The bus port (`LunaService`) is not tied to our cards. `enyo.PalmService`'s
  `call`/`subscribe`/`cancel` is a thin wrapper over it, and an HP app whose
  service code goes through that wrapper keeps working while its screens are
  replaced.
* The controls are custom elements, so enyo's own DOM-driven code can create
  and use them (`document.createElement("wos-toggle")`) without knowing what
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
* `tools/test-web.sh` type-checks with TypeScript 7 and runs the
  SDK's tests on `node --test` -- no browser, no bus.
* `tests/template-card.cpp` runs the built card in the engine WebAppMgr uses,
  against a fake `PalmServiceBridge`: it asks the bus, draws HP's rows, says so
  when a service is not running, and asks again when the user presses.
* `tests/kit-elements.cpp` covers what only exists in a browser: a property set
  before the definition arrived, a property set later repainting, a control
  slotted into a row not selecting it, the stylesheet meaning the same thing
  inside a shadow root as outside one, and a card's own CSS not reaching in --
  and, for the controls #58 added, that a long list draws only the window it
  shows, that a popup opened off the edge is clamped back into view, that a
  picker marks and reports its value, that a search field turns its magnifier
  into a clear cross, and that a dialog traps the focus and lets Escape close
  it rather than the card behind it.
* Everything above was checked by mutation.

**Not covered by a test**, and worth knowing: `startCard` itself -- what it
wires is covered through the two cards, not directly; and `AppService.banner`,
which needs a running WebAppMgr to mean anything.

The sizes, for the record: the template card is 31 KB of JavaScript (11 KB
gzipped) including `lit-html` and its own logic, plus 10 KB of CSS. enyo's core
alone is 388 KB of JavaScript and 60 KB of CSS, before the card's own code.
