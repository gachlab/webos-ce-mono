example-plain
=============

`com.gachlab.app.plain`. A card for this device written **without our runtime**:
no lit-html, no `defineElement`, no `startCard`. It builds its screen with
`document.createElement` and connects to the device with `connectCard`.

It exists to be checked, not to be used (#65).

enyo's mistake was not having layers — it was making the top one **compulsory**.
That is why porting an HP app today means rewriting it from scratch, and it is
the one thing this foundation must not repeat. "You could write a card in React
tomorrow" is easy to say and easy to stop being true, so it gets a file that
says it in code and a test that fails the day it stops.

What it demonstrates, and what `tests/plain-card.cpp` asserts:

1. **The controls dress themselves.** Nothing here hands the kit a stylesheet —
   importing `@webos/ui-kit/kit/kit.ts` is what defines the elements *and*
   styles them — so a `<wos-row>` built by hand comes out at HP's 52px like one
   built by our renderer. Take `useStyles(styles)` out of `kit/kit.ts` and that
   assertion reads `0px`.
2. **`connectCard` is all the platform asks.** The lifecycle, the back gesture,
   and telling WebAppMgr the card is on screen. What draws is the caller's
   business; here it is thirty lines of DOM.

```
src/main.ts    the whole card: logic, then drawing, with a line between them
```

`tools/test-web.sh` refuses an import of `start-card.ts` or `element.ts` from
this directory. Importing the kit's **elements** is what it is meant to do;
importing the machinery that draws them would leave the proof proving nothing.

```sh
tools/build-cards.sh com.gachlab.app.plain
ctest --test-dir build/tests -R plain-card
```
