wifi-enyo
=========

`com.gachlab.app.wifienyo`, the Wi-Fi settings card as it was. **Ours.** HP's card
was never released as source — it is not in the Apache-licensed core apps — so
it is not redistributed here. This one was written to give the same experience:

* the light header with the Wi-Fi icon and a switch for the radio;
* the network list, joining a secured network, joining a network by name, and
  the connected network's address settings — all of it enyo's `WiFiConfig`
  (`enyo-1.0/framework/lib/wifi`), which HP did release;
* "Connected to *name*. BSSID …, Channel …." above the connected network;
* **Settings** in the app menu, with the phone's **When Device Sleeps**: *Turn
  Wi-Fi Off* switches the radio off while the laptop is suspended;
* **Known Networks** in the app menu, each removable with a swipe;
* **Help**, which opens webOS Archive's copy of HP's help site.

The system menu opens it in two ways: from **Wi-Fi Preferences**, and by tapping
a network in the wifi drawer that needs it — a secured network with no saved
profile (the card opens on its join screen) or the joined network (the card opens
on its address settings). Both arrive as a `target` launch parameter.

help.palm.com is gone, and the archived copy has no Wi-Fi page of its own, so
Help opens the copy's English index.

Everything the card does goes through `com.palm.wifi`, answered by
`services/nm-connectionmanager`.

The icons are drawn for this card (`images/icon-source.svg`, rendered with
`rsvg-convert`); HP's showed a trademark that is not ours to ship.
