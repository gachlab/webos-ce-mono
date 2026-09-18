app-template
============

`com.gachlab.app.template`, the card the others are copied from, and it does
everything a card does: it asks `com.palm.deviceprofile` who this device is,
watches `com.palm.connectionmanager` while it is on screen and stops while it is
not, shows what came back in HP's rows, says so when a service is not running,
asks again when the user presses, and opens a second screen that the back
gesture comes out of before the card closes.

It is what `tests/template-card.cpp` drives in the engine WebAppMgr uses, so the
shape a new app starts from is the shape that is tested.

```
src/main.ts               where the service and the view are wired up
src/template.service.ts   the logic, with no idea what draws it
src/appinfo.json          the id, and what the launcher shows
test/                     the service, with a fake bus
```

```sh
tools/build-cards.sh com.gachlab.app.template   # into build/cards/<id>/
tools/test-web.sh                               # type check and the tests
```

It also opens in an ordinary browser: with no `PalmServiceBridge` on the page,
`openBus()` hands the service a fake instead, so a screen can be written and
looked at without a session.
