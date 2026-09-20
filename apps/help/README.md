help
====

`com.gachlab.app.help`, the Help settings card (#22): a short TOC that opens
`https://help.webosarchive.org/en-us/` (with optional topic paths) via
`applicationManager/open`.

**Ours.** HP's Help app is the specification; nothing from that image is in
this tree. The shell still launches `com.palm.app.help`; this app answers that
id through `aliases`.

```
src/main.ts           the TOC list
src/help.service.ts   open topic URLs
src/luna/help.ts      topics + applicationManager/open
test/                 the service, with a fake bus
```

```sh
tools/build-cards.sh com.gachlab.app.help
tools/test-web.sh apps/help/test/help.service.test.ts
```
