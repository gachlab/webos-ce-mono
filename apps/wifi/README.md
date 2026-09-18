wifi
====

`com.gachlab.app.wifi`, the Wi-Fi settings card (#38), on the web foundation:
the network list, joining a secured network, joining one by name, the radio
switch, and the connected network's address settings.

**Ours.** HP's card was never released as source, so this one was written from
the experience rather than from the code. The enyo card it replaces is
`apps/reference/wifi-enyo` — kept, not installed, and the other side of the A/B
that says whether this one looks right.

```
src/main.ts           the screens, and what each control answers with
src/wifi.service.ts   the logic: scanning, joining, failing, the radio
src/luna/wifi.ts      com.palm.wifi, typed -- only this app talks to it
src/wifi.css          what the kit does not already draw
test/                 the service, with a fake bus
```

```sh
tools/build-cards.sh com.gachlab.app.wifi   # into build/cards/<id>/
tools/test-web.sh                           # type check and the tests
```

`tests/wifi-card.cpp` drives the built card in the engine WebAppMgr uses,
against a fake bridge that can push a subscription at it mid-flight.
