vpn
===

`com.gachlab.app.vpn`, the VPN settings card (#21), on the web foundation: the
profile list, adding an OpenVPN or WireGuard profile, connection details, and
connect / disconnect. The system menu drawer talks to the same
`com.palm.vpn` this card uses.

**Ours.** HP's card was never released as source. The experience on the TouchPad
CE 3.1.0 image is the specification; nothing from that image is in this tree.
The shell still launches `com.palm.app.vpn`; this app answers that id through
`aliases`.

```
src/main.ts          the screens
src/vpn.service.ts   list / add / details / connect
src/luna/vpn.ts      com.palm.vpn, typed
src/vpn.css          what the kit does not already draw
test/                the service, with a fake bus
```

```sh
tools/build-cards.sh com.gachlab.app.vpn
tools/test-web.sh
```
