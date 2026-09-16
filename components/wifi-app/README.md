wifi-app
========

`com.palm.app.wifi`, the Wi-Fi settings card. **Ours.** HP's card of the same id
was never released as source — it is not in the Apache-licensed core apps — so
it is not redistributed here. This one was written to give the same experience:

* the light header with the Wi-Fi icon and a switch for the radio;
* the network list, joining a secured network, joining a network by name, and
  the connected network's address settings — all of it enyo's `WiFiConfig`
  (`enyo-1.0/framework/lib/wifi`), which HP did release;
* "Connected to *name*. BSSID …, Channel …." above the connected network;
* **Known Networks** in the app menu, each removable with a swipe.

The system menu opens it in two ways: from **Wi-Fi Preferences**, and by tapping
a network in the wifi drawer that needs it — a secured network with no saved
profile (the card opens on its join screen) or the joined network (the card opens
on its address settings). Both arrive as a `target` launch parameter.

Left out, because a laptop has nothing behind them: the phone's **When Device
Sleeps** setting and the help link, whose site no longer exists.

Everything the card does goes through `com.palm.wifi`, answered by
`components/nm-connectionmanager`.

The icons are drawn for this card (`images/icon-source.svg`, rendered with
`rsvg-convert`); HP's showed a trademark that is not ours to ship.
