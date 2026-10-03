bluetooth
=========

`com.gachlab.app.bluetooth`, the Bluetooth settings card (#31), on the web
foundation: the radio switch, the trusted and discovered devices, pairing
(PIN, passkey, numeric comparison), connect / disconnect, and forget. The
system menu's Bluetooth drawer talks to the same `com.palm.btmonitor` and
`com.palm.bluetooth` this card uses.

**Ours.** HP's card (`com.palm.app.bluetoothtab`) was never released as source.
The experience on the TouchPad CE 3.1.0 image is the specification; nothing from
that image is in this tree, and the icon is drawn anew (`images/icon-source.svg`
is the Bluetooth mark on a glossy tile, rasterised to the three PNGs). The shell
still launches `com.palm.app.bluetoothtab`; this app answers that id through
`aliases`.

```
src/main.ts              the screens
src/bluetooth.service.ts radio / devices / pairing / details
src/luna/bluetooth.ts    com.palm.btmonitor and com.palm.bluetooth, typed
src/bluetooth.css        what the kit does not already draw
test/                    the service, with a fake bus
```

The pairing screen renders whichever Agent1 prompt the service raises: numeric
comparison and incoming authorization are yes/no; a passkey or PIN takes a typed
value; a display-only passkey/PIN shows the code with an OK. The answers go back
through `gap/supplyconfirmation`, `supplypasskey` and `supplypincode`.

What is modern, carried from BlueZ and shown here where HP's card could not:
the battery percentage under a connected device, and the resolved device icon.

```sh
tools/build-cards.sh com.gachlab.app.bluetooth
tools/test-web.sh
```
