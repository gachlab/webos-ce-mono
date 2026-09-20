dateandtime
===========

`com.gachlab.app.dateandtime`, the Date & Time settings card (#22): time format,
network time, manual set, and timezone picker.

**Ours.** HP's card is the specification; nothing from that image is in this
tree. The shell still launches `com.palm.app.dateandtime`; this app answers that
id through `aliases`.

```sh
tools/build-cards.sh com.gachlab.app.dateandtime
tools/test-web.sh apps/dateandtime/test/dateandtime.service.test.ts
```
