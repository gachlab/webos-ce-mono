@webos/api
==========

What an app for this device is written against: the bus, the app's own life,
launch parameters and translation. **No DOM.** Nothing here knows there is a
screen, which is what makes it possible to write a card for this device in
something that is not our kit.

That direction is not a convention any more. `@webos/ui-kit` depends on this
package; this package depends on nothing of ours, and `tools/test-web.sh` fails
if a file here so much as mentions the kit.

```
src/helpers/     state, timers, and a subscription that follows the card
src/i18n/        t("Turn on Wi-Fi") -- the key is the English text
src/infra/luna/  the bus, and one typed module per webOS service
src/infra/app/   the card's own life, on window.Mojo and PalmSystem
src/services/    navigation: which screen the card is showing
```

* `helpers/create-state.ts` — every service's state: `{name, data, error}`, and
  a subscribe that hands the current state over at once, so a card paints its
  first frame without waiting. An error belongs to the state it happened in: a
  new name leaves it behind.
* `helpers/timers.ts` — the clock, as something a test can hand over.
* `helpers/watch.ts` — a subscription that follows the card: on while it is
  being looked at, off while it is not. A card that is sent away keeps its
  page; what it must not keep is a service pushing at it.
* `infra/luna/service.ts` — the bus as a card sees it: `call` and `subscribe`,
  `LunaCallError`, `LunaTimeout`. `bridge.service.ts` is WebAppMgr's
  `PalmServiceBridge`; `fake.service.ts` is what tests and a plain browser talk
  to.
* `infra/luna/<service>.ts` — one webOS service, typed: its uris, its reply
  shapes and its quirks, in one place. `connectionmanager.ts` is the one every
  card needs. A service that only one app talks to lives in that app, the way
  `apps/wifi/src/luna/wifi.ts` does.
* `infra/app/connect-card.ts` — **the entry point**: `connectCard` is
  everything being a card on this device means — the lifecycle, `stageReady`,
  the back gesture, letting go when the page unloads — and nothing about how it
  is drawn. Its `paint` callback is the seam a renderer hooks into, ours or
  anyone's. `@webos/ui-kit`'s `startCard` is this plus one line;
  `apps/example-plain` is a card that calls this one directly and draws itself.
* `infra/app/service.ts` — what it was launched with, being brought to the
  front and sent away, relaunched with new parameters, the keyboard taking half
  the screen, the back gesture, and telling WebAppMgr the card is ready.
  `palm-system.service.ts` is that on `window.Mojo` and `PalmSystem`, keeping
  whatever hooks were already on the page.
* `services/navigation.service.ts` — HP's cards are a stack: a row opens a
  screen, back pops it, and the card closes when there is nothing left to pop.

Tests: `tools/test-web.sh`, which type-checks with TypeScript 7 and runs
`node --test` over `test/`. They import through `@webos/api/...`, the same
front door an app uses, so a subpath that stops being reachable turns them red.
