bluetooth
=========

`com.palm.btmonitor` and `com.palm.bluetooth`, answered from BlueZ instead of
from HP's device-only services.

This is **ours, not HP's**. HP's `com.palm.btmonitor` and `com.palm.bluetooth`
shipped on the TouchPad and were never released as source, so nothing of theirs
is in this tree. The specification is the one surviving consumer: the system
menu's Bluetooth drawer in `reference/luna-sysmgr-ce`'s
`StatusBarServicesConnector.cpp`, read request by request. The payloads here
answer exactly what it parses, so the radio toggles and the device list fills
without the shell being touched. (#31)

Who is listening
----------------

Found by reading the connector, not guessed. The drawer calls, and acts on:

| Call | What it does with it |
|---|---|
| `com.palm.btmonitor/monitor/subscribenotifications` | the status-bar icon, from `radio` and the `notifn*` strings |
| `com.palm.btmonitor/monitor/radioon` \| `radiooff` | the on/off row |
| `com.palm.bluetooth/gap/gettrusteddevices` | the trusted-device list |
| `com.palm.bluetooth/gap/subscribenotifications` | list changes |
| `com.palm.bluetooth/prof/profgetstate` | which profiles are connected |
| `com.palm.bluetooth/prof/subscribenotifications` | per-connection transitions |
| `com.palm.bluetooth/prof/profconnect` \| `profdisconnect` | tapping a device |

The seven profile names are HP's own vocabulary, not BlueZ's: `hfg a2dp pan hid
spp hf mapc`. BlueZ speaks 128-bit SIG UUIDs, so `bluez_state.h` bridges the two
(`profileFromUuid`, `uuidForProfile`); the UUIDs were confirmed against real
devices on the development machine.

Why C++
-------

The same reason as `services/nm-connectionmanager`: this needs a D-Bus client,
and gio already provides one to anything linking glib, which the service does
for its main loop. The JavaScript route meant bundling a D-Bus library first.

How it reads BlueZ
------------------

`src/bluez_state.h` holds the mapping, free of both buses, so
`tests/bluez-state.cpp` checks every decision without a D-Bus daemon and without
ls-hubd. `src/bluez_client.cpp` is everything said to BlueZ -- one
`GetManagedObjects` on the ObjectManager reads the adapter and every device
(and `Battery1`, which lives on a separate interface of the same object) in one
round trip -- and takes the D-Bus connection as an argument, so
`tests/bluez-client.py` runs it against a fake BlueZ on a private bus.
`src/main.cpp` hands it the system bus and answers the webOS bus, owning both
names in one process so they can never disagree about the radio.

What modern BlueZ adds
----------------------

HP's stack was BR/EDR only. BlueZ 5 is read for what the card uses and the
system menu ignores, carried through rather than dropped to fit the old
contract:

* **Battery level** (`Battery1.Percentage`), on the trusted-device entries.
* **The LE fields** -- `addressType`, and discovery with a transport filter
  (`gap/startdiscovery {"transport":"le"|"bredr"|"auto"}`), on top of HP's
  unfiltered scan.
* **Modern pairing** -- `gap/pair`, `cancelpairing`, `removedevice`,
  `settrusted` on `Device1.Pair`/`CancelPairing`/`RemoveDevice`/`Trusted`.
* **Per-UUID connect** -- `profconnect` connects just the profile's UUID
  through `Device1.ConnectProfile`, falling back to `Connect` for the whole
  device (`profile: "all"`, or a device with no UUID for the profile).
* **The resolved `icon`** BlueZ computes (e.g. `audio-headset`).

Testing it
----------

```sh
ctest --test-dir build/tests -R 'bluez-state|bluez-client' --output-on-failure
```

`bluez-client` needs `python-dbusmock` and `dbus-daemon`: the bluez5 template
serves a fake BlueZ on a private bus, extended per device with the fields HP's
stack never had (`Trusted`, a real `Class`, `UUIDs`, `Battery1`). The service's
own GDBus client is exercised through `tests/bluez-probe`, so the C++ under test
is the C++ that ships. Absent `python-dbusmock`, the test SKIPs (exit 77) rather
than failing the build.

Verified by mutation: breaking the a2dp UUID in `uuidForProfile` fails both the
pure test (the profile mapping) and the integration test (which captures the UUID
`ConnectProfile` receives); letting an untrusted device into the trusted list,
or dropping the `Class`->`cod` conversion, each make `bluez-state` fail.
