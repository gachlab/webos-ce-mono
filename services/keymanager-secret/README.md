keymanager-secret
=================

`com.palm.keymanager`, answered from the host's Secret Service
(`org.freedesktop.secrets`) instead of from a native encrypted store a phone's
lock-screen passcode unlocked.

This is **ours, not HP's**. HP's keymanager shipped only on the device and was
never released as source; nothing in the CE drop provides the name, so three
callers have been talking to a service that is not there and failing quietly.

Who is listening
----------------

Found by reading the code, not guessed:

| Caller | What it calls, and with what |
|---|---|
| LunaSysMgr's `Security.cpp` | `initialize {password}` and `changePassword {oldPassword,newPassword}`, from the lock screen, fire-and-forget. It also subscribes to `registerServerStatus` for `com.palm.keymanager` and re-runs `initialize` whenever the service (re)appears — so the name **must** register for real. |
| the accounts service (`credentials-model_keymanager.js`) | the KeyStore the whole Synergy credential stack sits on: `fetchKey`, `store`, `remove`, `keyInfo`, all keyed on `{keyname}`. `put` does `fetchKey` → `remove` → `store`; `get` does `fetchKey` and `JSON.parse`es `keydata`; `has` does `keyInfo`. |
| enyo's `PalmServices` | the alias `crypto → palm://com.palm.keymanager`. No live method caller in the tree, but it confirms enyo's expectation. |

The methods
-----------

| Method | Request | Reply |
|---|---|---|
| `initialize` | `{password}` | `{returnValue:true}` once the store is reachable |
| `changePassword` | `{oldPassword,newPassword}` | `{returnValue:true}` |
| `store` | `{keyname,keydata,type,nohide}` | `{returnValue:true}` |
| `fetchKey` | `{keyname}` | `{returnValue:true,keydata:"<blob>"}`, or a failure when absent |
| `remove` | `{keyname}` | `{returnValue:true}` (removing nothing is still success) |
| `keyInfo` | `{keyname}` | `{returnValue:true}` when present, a failure when absent |

A missing key is a **failure**, not an empty blob: the accounts KeyStore's
`getCredentials` and `hasCredentials` both key off the call's future throwing, so
`fetchKey`/`keyInfo` on an unstored key reply `returnValue:false`, which they read
as "no credentials" / `has()=false`.

The store
---------

One Secret Service item per `keyname`, tagged with the schema
`com.gachlab.webos.keymanager` so a keyring full of a browser's and a mail
client's own secrets is never read, replaced or deleted — only what this service
wrote. The blob is the item's secret value; the item's label is the `keyname`,
so a human in GNOME's Seahorse can tell which account a blob belongs to.

`src/key_store.h` is the mapping and the payloads, free of both buses, so
`tests/key-store.cpp` checks every decision without a keyring and without
ls-hubd. `src/secret_client.cpp` is everything said to the Secret Service and
takes the D-Bus connection as an argument, so `tests/secret-client.cpp` runs it
against a fake `org.freedesktop.secrets` on a private bus. `src/main.cpp` hands
it the session bus and answers the webOS bus.

Two things worth knowing:

* **The session bus, not the system bus.** The Secret Service is a per-login
  daemon (gnome-keyring, KWallet's Secret Service front, KeePassXC), so this is
  the one service here that reaches for `G_BUS_TYPE_SESSION`. No session bus
  leaves the store unreachable, and every call then reports "no session bus"
  rather than crashing — the honest state of a store that is not there.
* **The webOS passcode does not own the keyring's password.** HP's lock screen
  unlocked the keystore with the passcode; the host keyring is unlocked by the
  user's own login (PAM) or its own prompt. `initialize` and `changePassword`
  therefore make sure the default collection is present and unlock it if the
  keyring will do so without a prompt — they do **not** set the keyring's
  password, which would lock the user out of secrets this service does not own.

Why C++
-------

The store is reached over D-Bus, and gio ships a D-Bus client with glib, which
every service here already links for its main loop — the same reasoning as
`services/nm-connectionmanager`. There is no `libsecret`: gio's own client is
enough, and `libsecret-1` is not on the build host anyway.

Testing it
----------

```sh
ctest --test-dir build/tests -R 'key-store|secret-client' --output-on-failure
```

`secret-client` needs `dbus-daemon`: GLib's `GTestDBus` starts a private one for
the fake Secret Service, so the user's real keyring is never touched.

The mapping is verified by mutation: dropping the `keydata` requirement from a
store, failing to escape a quote in a fetched blob, letting a `remove` on an
empty keyring report failure, or returning an empty blob from a fetch each make
a test fail.
