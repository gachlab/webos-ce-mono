swmanager
=========

`com.gachlab.app.swmanager`, the Software Manager settings card (#22): list
installed apps from `applicationManager/listApps`, show details, and remove via
`appinstaller/remove` when `removable`.

**Ours.** HP's App Catalog manager is the specification; nothing from that
image is in this tree. The shell still launches `com.palm.app.swmanager`; this
app answers that id through `aliases`.

```
src/main.ts               the screens
src/swmanager.service.ts  list / details / remove
src/luna/swmanager.ts     applicationManager + appinstaller
test/                     the service, with a fake bus
```

```sh
tools/build-cards.sh com.gachlab.app.swmanager
tools/test-web.sh apps/swmanager/test/swmanager.service.test.ts
```
