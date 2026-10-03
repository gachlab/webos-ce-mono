zeroconf-avahi
==============

`com.palm.zeroconf`, answered from the host's Avahi (`org.freedesktop.Avahi`)
instead of from HP's device-only service.

This is **ours, not HP's**. HP's zeroconf shipped only on the TouchPad and was
never released as source.

A provisional API
-----------------

Unlike every other service in this tree, **the method and payload names here are
not an HP contract.** HP's `com.palm.zeroconf` was never released, and nothing
in this tree invokes a method on it — enyo's framework carries only the URI
alias (`PalmServices.js`: `zeroconf → palm://com.palm.zeroconf/`) with no
consumer. There is therefore no surviving specification to mirror, which is why
this was split out of #43's first pass.

The names below are the DNS-SD vocabulary every mDNS library converges on
(browse / resolve / register), paired with this tree's own subscription idiom
(as `com.palm.btmonitor`'s `subscribenotifications` has). They are **provisional**:
the first real caller, or a chosen API, gets to rename them, and
`src/zeroconf_records.h` is the one place that changes when it does.

The methods
-----------

| Method | Request | Reply |
|---|---|---|
| `browse` | `{serviceType:"_ipp._tcp", subscribe?}` | `{returnValue:true, services:[{name,type,domain}]}`; a subscriber is pushed the full set again on each change, with `changed` = `"added"` / `"removed"` / `"complete"` |
| `resolve` | `{name, serviceType, domain?}` | `{returnValue:true, name, type, domain, hostname, address, port, txt:{...}}` |
| `register` | `{name, serviceType, port, txt?, subscribe?}` | `{returnValue:true, registered:true, name, type}` |
| `unregister` | `{name, serviceType, port}` | `{returnValue:true}` |

A `serviceType` is `_service._tcp` or `_service._udp`; a malformed one is a
request error, not an empty result. `register` keeps the service published as
long as the client is subscribed (or until `unregister`): Avahi withdraws it the
moment the entry group is freed, so a crashed publisher leaves no stale record.

How it reads and writes
-----------------------

`src/zeroconf_records.h` is the vocabulary, the request guards, the TXT
splitting and the payloads, free of both buses, so `tests/zeroconf-records.cpp`
checks every decision without an Avahi daemon and without ls-hubd.
`src/avahi_client.cpp` is everything said to Avahi and takes the D-Bus
connection as an argument, so `tests/avahi-client.cpp` runs it against a fake
Avahi on a private bus. `src/main.cpp` hands it the system bus and answers the
webOS bus.

Two things worth knowing:

* **Browse is Avahi's cache, not a fresh scan each call.** `ServiceBrowserNew`
  returns a live object the daemon drives with `ItemNew`/`ItemRemove`/
  `AllForNow`; one browser per service type is shared by every subscriber of
  that type, since a second browser for the same type is wasted work. A one-shot
  browse borrows the same cache and replies immediately. The same instance seen
  on two interfaces is reported once — webOS has no notion of per-interface
  instances.
* **TXT is Avahi's array of byte strings.** Each entry is `key=value` (or a bare
  `key`), split on the first `=`; a leading `=` (empty key) is dropped.

Why C++
-------

The same reason as `services/nm-connectionmanager` and `services/bluetooth`: it
needs a D-Bus client, and gio already provides one to anything linking glib,
which the service does for its main loop. No `avahi-client` library — gio's own
D-Bus client is enough.

Testing it
----------

```sh
ctest --test-dir build/tests -R 'zeroconf-records|avahi-client' --output-on-failure
```

`avahi-client` needs `dbus-daemon`: GLib's `GTestDBus` starts a private one for
the fake Avahi, so the host's real mDNS daemon is never touched.

Verified by mutation: letting `validServiceType` accept a non-tcp/udp proto, or
dropping the erase from `ItemRemove` (so a vanished service lingered), each make
a test fail.
