nm-connectionmanager
====================

`com.palm.connectionmanager`, answered from NetworkManager instead of from a
constant.

This is **ours, not HP's**. The CE drop ships
`components/pmnetconfigmanager-stub`, whose entire implementation is one
JavaScript function that replies:

```js
"isInternetConnectionAvailable": true,
"wifi": { "state": "connected", "ipAddress": "192.168.0.0", "ssid": "Open webOS", ... }
```

always, whatever the machine is actually doing. Pull the cable, switch wifi off,
sit behind a captive portal: nothing changes, and the email app keeps trying to
sync against a network that is not there.

Who is listening
----------------

All four subscribe with `{"subscribe":true}` and act on what arrives. They were
found by reading the code, not guessed:

| Caller | What it does with it |
|---|---|
| `StatusBarServicesConnector.cpp` | the wifi indicator, from `wifi.state` and `wifi.onInternet` |
| luna-sysservice's `NetworkConnectionListener` | fires `connectionStateChanged`, which is how the rest of the system learns it is offline. Declares `isInternetConnectionAvailable` REQUIRED and drops a payload without it |
| `BrowserServer.cpp` | `isInternetConnectionAvailable` |
| activitymanager's `ConnectionManagerProxy` | the `wifi`, `wan` and `*Confidence` requirements activities are scheduled against |

Note the two spellings: activitymanager calls `getStatus`, everyone else calls
`getstatus`. Both are registered, as the stub's `services.json` did.

Why C++
-------

Not because of subscriptions. Those were **measured to work** from a mojoservice
JavaScript service in this port: with `"subscribe": true` declared on the command
and the subscription factory used, a subscriber received every pushed update.
That is different from LS2 *signals*, which `palmbus` genuinely cannot emit and
which is why `components/sysfs-powerd` is C++.

The reason is the D-Bus client. gio ships one with glib, which every service here
already links for its main loop, so reading NetworkManager costs no new
dependency. The JavaScript route meant bundling a D-Bus library and building the
npm and esbuild machinery to vendor 15 packages, before writing a line of network
logic. That machinery belongs to the ticket that actually needs it — Synergy and
its OAuth connectors — not to this one.

How it reads the network
------------------------

`src/network_state.h` holds the mapping, free of both buses, so
`tests/network-state.cpp` can check every decision without a D-Bus daemon and
without ls-hubd. `src/main.cpp` fills it in from NetworkManager and answers the
bus.

Two things are worth knowing, both measured on the machine this was written for:

* **The transport is found by walking the devices, not by asking NetworkManager
  which connection is primary.** With a VPN up, `PrimaryConnection` is the tunnel
  (`tun0`) and `PrimaryConnectionType` is `"vpn"`, while what webOS can act on is
  the wifi underneath it. Devices are selected by type: 1 is ethernet, 2 is wifi,
  and the rest this machine reports — 13 bridge, 16 tun, 20 veth, 30 wifi-p2p,
  32 loopback — are not transports webOS has any notion of.
* **A captive portal is not the internet.** `isInternetConnectionAvailable`
  follows NM's `Connectivity`, so a portal reads as connected wifi with
  `onInternet: "no"` and no internet, which is what stops the email app from
  syncing against a login page.

The one field that is not a preference
--------------------------------------

`StatusBarServicesConnector.cpp` does this, with no null check:

```c
if(!strcmp(state, "connected") && !strcmp(onInternet, "yes"))
```

on the raw `const char*` it pulled out of the `wifi` object. A `wifi` object
without either field as a string is not a wrong icon — it is a null dereference
in the shell. Both are therefore written in every payload, even when there is no
wifi device at all, and the test asserts it for every state it builds.

Testing it
----------

```sh
ctest --test-dir build/tests -R network-state --output-on-failure
```

The mapping is verified by mutation: removing the escape for a quote in an SSID,
moving a confidence threshold, letting a captive portal count as internet,
dropping `onInternet`, or ignoring whether the device is up each make it fail.
