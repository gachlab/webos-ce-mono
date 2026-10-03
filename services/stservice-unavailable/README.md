stservice-unavailable
=====================

`com.palm.stservice` (Touch to Share), answered as **unavailable** — cleanly,
so the callers do not hang or log a missing service.

This is **ours, not HP's**. HP's Touch to Share service shipped only on the
device and was never released; nothing in the CE drop provides the name.

What it was
-----------

On the TouchPad the browser called `shareData` with the current page, and
`tap2share` carried it over Bluetooth to a paired Pre 3; the system UI showed
the incoming share and drove its dialogs through the responder methods. This
machine has no tap sensor and no paired phone, and ticket #43 leaves the backend
undecided. Its "done when" lists, as an acceptable close, that the service
"answers unavailable cleanly" — which is what this does, and nothing more.

Who is listening
----------------

| Caller | What it calls |
|---|---|
| isis-browser's `BrowserApp.js` | `shareData {data:{target,type,mimetype}}`, from the Share menu |
| luna-systemui's `SystemManagerAlerts.js` | `acceptShareRequest`, `rejectShareRequest` (the incoming-share dialog), and `tryAgain`, `cancel`, `bluetoothIsOffResponse` (the connection dialog) |

All are fire-and-forget `enyo.PalmService` calls with no success or failure
handler, so "clean" means the name is registered and every method replies —
not that the reply carries anything the caller reads.

The two replies
---------------

| Methods | Reply | Why |
|---|---|---|
| `shareData` | `{returnValue:false,errorCode:-1,errorText:"Touch to Share is not available on this device"}` | an action that cannot be carried out. `returnValue:false` tells the browser the page was not shared; HP's own `shareData` could fail (Bluetooth off, no peer), so a failing reply is within its contract, not a new shape. |
| the responder methods | `{returnValue:true}` | acknowledgements of a dialog the user dismissed. Nothing was pending, but a dismissal is not an error, so the UI closes rather than treating its own cancel as a failure. |

`src/share_replies.h` holds both, free of the bus, so `tests/share-replies.cpp`
pins them without ls-hubd. `src/main.cpp` is just the registration and the
method table.

When a backend is chosen (sharing with a paired phone was the sketch in #43),
`share_replies.h` is what it replaces; the shape of the service — one name,
these six methods — is already what its callers expect.

Testing it
----------

```sh
ctest --test-dir build/tests -R share-replies --output-on-failure
```

Verified by mutation: making `shareData` reply `returnValue:true` (so the
browser would believe an un-sent page was shared) makes the test fail.
