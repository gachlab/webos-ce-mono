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
without ls-hubd. `src/nm_client.cpp` is everything said to NetworkManager —
what is read, and the calls that connect and disconnect the cable — and takes the
D-Bus connection as an argument, so `tests/nm-client.cpp` runs it against a fake
NetworkManager on a private bus. `src/main.cpp` hands it the system bus and
answers the webOS bus.

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

com.palm.wifi
-------------

The same process owns `com.palm.wifi`, so the two names can never disagree about
the radio. Its callers are the system menu's wifi drawer and enyo's wifi library
(`enyo-1.0/framework/lib/wifi`), and the vocabulary is theirs:

| Method | What it does |
|---|---|
| `getstatus` | subscribable; `serviceDisabled`, `serviceEnabled`, or `connectionStateChanged` with `networkInfo`, plus `apInfo` (BSSID and channel) once joined |
| `setstate` | `{"state": "enabled" \| "disabled"}`, NetworkManager's `WirelessEnabled` |
| `findnetworks` | the networks in range, one entry per name, strongest access point first after the joined one, with the saved `profileId` of each |
| `connect` | `{"profileId": n}`, or `{"ssid": s}` with the security either top-level (the menu) or under `security.simpleSecurity` (the library) |
| `getprofile` | a saved wifi profile, and the address in use when it is the active one |
| `deleteprofile` | a saved wifi profile, by id |
| `getprofilelist` | every saved wifi profile, for the settings card's known networks |
| `getinfo` | the radio's MAC address |

A `profileId` is the number that ends NetworkManager's settings path
(`/org/freedesktop/NetworkManager/Settings/12`). Joining a network that already
has a profile reuses it — with a new key, only its security is replaced, so
settings made in GNOME survive.

Supported: open networks, WPA/WPA2 personal, WPA3 personal (SAE, chosen from what
the access point advertises, since the user only ever types a password), WEP, and
enterprise (802.1X): PEAP, TTLS and FAST with a user name and password checked
with MSCHAPv2 ("Auto" offers PEAP and TTLS), and TLS with a certificate. Checking
the server's certificate checks it against the system's CAs. A rejected
enterprise login is reported as `IncorrectPassword`, a rejected key as
`IncorrectPasskey`.

The settings card's address screen sends a saved profile back with
`useStaticIp`: the profile's `ipv4` is replaced — DHCP, or the address, mask,
gateway and DNS servers given — and the profile brought up again.

`com.palm.certificatemanager/listcertificates` — a third name owned by the same
process — lists the certificates a TLS login can use: the PEM files, each with
its unencrypted key, in `$WEBOS_CERTIFICATE_DIR` or
`~/.local/share/webos-ce/certificates`.

Three guards that are deliberate:

* **Only wifi profiles.** `getprofile`, `deleteprofile` and `connect` refuse a
  profile whose type is not `802-11-wireless`: the cable and a VPN live in the
  same list, and this is not their API.
* **`deleteprofile` needs an id.** enyo's library also calls it with no
  arguments, which on the phone meant every saved network. Here that would be
  the user's NetworkManager profiles.
* **A failed join is still a failure once NetworkManager has moved on.** NM goes
  from FAILED to DISCONNECTED within a second, keeping the reason; the service
  reads the state once per burst, so FAILED is often never seen. A disconnected
  radio whose reason is a join failure, while a join was requested, is reported
  as `associationFailed` with `lastConnectError` — `IncorrectPasskey` for the
  supplicant disconnecting, timing out or asking for secrets again — and named
  after the network being joined, because the library ignores a failure that
  does not name it.

When Device Sleeps
------------------

`com.palm.connectionmanager/getWakeOnWiFiMode` and `setWakeOnWiFiMode`, with
`"enable"` or `"disable"`, are what the settings card's **When Device Sleeps**
reads and writes. The mode is kept in
`/var/luna/preferences/com.palm.connectionmanager.wakeonwifi`.

With `"disable"` (*Turn Wi-Fi Off*), `src/sleep_watch.cpp` holds a logind
`delay` inhibitor. On `PrepareForSleep(true)` the radio is switched off — only
if it was on — and the lock released, which is what lets the machine sleep; on
`PrepareForSleep(false)` the radio comes back and the lock is taken again. A
radio the user had switched off stays off.

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
ctest --test-dir build/tests -R 'network-state|nm-client' --output-on-failure
```

`nm-client` needs `dbus-daemon`: GLib's `GTestDBus` starts a private one for the
fake NetworkManager, so the host's network is never touched.

The mapping is verified by mutation: removing the escape for a quote in an SSID,
moving a confidence threshold, letting a captive portal count as internet,
dropping `onInternet`, or ignoring whether the device is up each make it fail.
