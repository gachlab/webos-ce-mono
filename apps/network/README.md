network
=======

`com.gachlab.app.network`, the Networking card (#23): captive-portal sign-in and
per-network proxy settings. Hidden from the launcher (`visible: false`); opened
by enyo's `lib/networkproxy`, the Wi-Fi card's **Configure Proxy**, or when
`com.palm.connectionmanager` reports `onInternet: "captivePortal"`.

**Ours.** HP's Networking app was never released as source. The experience on
the TouchPad CE image is the specification. The shell still launches
`com.palm.app.network`; this app answers that id through `aliases`.

Screens
-------

* **Network Login** — note plus **Open Login Page**, which opens the captive
  probe URL `http://connectivitycheck.gstatic.com/generate_204` through
  `applicationManager/open` (the same check Android uses; Firefox's
  `detectportal.firefox.com/success.txt` is an alternative). The card watches
  `getstatus`; when `onInternet` leaves `captivePortal`, it shows that the
  network is connected.
* **Configure Proxy** — None / Manual / Automatic (PAC URL) / Auto-detect,
  saved with `configureNwProxies`. Launch with
  `{ mode: "proxy", networkTechnology, proxyScope }` (wifi scope = profileId).

```
src/main.ts             the screens
src/network.service.ts  portal / proxy state
src/luna/connection.ts  connectionmanager, typed
test/                   the service, with a fake bus
```

```sh
tools/build-cards.sh com.gachlab.app.network
tools/test-web.sh
```

WebAppMgr applies the active wifi entry from this store as Qt's application
proxy (manual immediately; PAC via Chromium flags when the engine starts). See
`services/nm-connectionmanager/README.md`.
