certificate
===========

`com.gachlab.app.certificate`, the Certificate Manager settings card (#22): the
user certificate list, details, and trusting a file via
`com.palm.certificatemanager`.

**Ours.** HP's Mojo card is the specification; nothing from that image is in
this tree. The shell still launches `com.palm.app.certificate`; this app answers
that id through `aliases`.

```
src/main.ts                 the screens
src/certificate.service.ts  list / details / add
src/luna/certificate.ts     com.palm.certificatemanager, typed
src/certificate.css         add footer
test/                       the service, with a fake bus
```

```sh
tools/build-cards.sh com.gachlab.app.certificate
tools/test-web.sh apps/certificate/test/certificate.service.test.ts
```
