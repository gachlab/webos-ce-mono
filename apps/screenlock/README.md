screenlock
==========

`com.gachlab.app.screenlock`, the Screen & Lock settings card (#22): brightness,
auto-dim, display timeout, wallpaper, advanced gestures, secure unlock, and
lock-screen notifications.

**Ours.** HP's card is the specification; nothing from that image is in this
tree. The shell still launches `com.palm.app.screenlock`; this app answers that
id through `aliases`.

```
src/main.ts                 the screens
src/screenlock.service.ts   prefs / lock / display
src/luna/screenlock.ts      display, systemservice, systemmanager
src/screenlock.css          notes and configure footer
test/                       the service, with a fake bus
```

```sh
tools/build-cards.sh com.gachlab.app.screenlock
tools/test-web.sh apps/screenlock/test/screenlock.service.test.ts
```
