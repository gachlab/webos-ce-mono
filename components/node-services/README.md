node-services
=============

webOS services in modern TypeScript (#39). **Ours**, Apache 2.0.

HP's JavaScript services (`components/app-services`) run on a stack built for
node 0.4: MojoLoader, `mojoservice`, `Foundations` futures and HP's `palmbus`
callbacks. This component is what replaces that stack, one service at a time,
keeping each service's API exactly as it is.

* **No build step.** The pinned node (`tools/node-version`) runs the `.ts`
  files as they are by stripping their types, so the code stays within what
  stripping handles (`erasableSyntaxOnly`). TypeScript 7 (`npm ci`, pinned in the
  root `package.json`) only checks the types.
* **Functional.** No classes. Every module exposes `createX(deps)`, which
  returns the thing itself; the default wiring is exported next to it
  (`openBus`, `openHandle`). Tests pass fakes through the same door.
* **Imports** go through `#kit/*`, mapped in this directory's `package.json`.

The kit
-------

`kit/luna.ts` — the bus.

```ts
const bus = openBus("com.example.service");

bus.method("add", ({ payload }) => ({ sum: payload.a + payload.b }));

// A subscription is an async generator. Each yield is one reply; when the
// subscriber goes away, request.signal is aborted.
bus.method("watch", async function* ({ signal }) {
    for await (const change of bus.subscribe(uri, payload, { signal })) yield change;
});

const reply = await bus.call("luna://com.palm.db/find", { query });
bus.exitWhenIdle(5000);
```

* A reply gets `returnValue: true` unless the handler set it. A thrown
  `lunaError(code, text, extra)` becomes `{returnValue: false, errorCode,
  errorText, ...extra}`; any other exception becomes `errorCode: -1` with its
  message. On the calling side, `returnValue: false` becomes a LunaError
  (`isLunaError`).
* A plain call to a generator method gets its first yield, and the generator is
  closed. A subscribed call gets every yield.
* A generator cannot be stopped while it waits, only at a yield: that is why
  handlers get `signal`, and why `subscribe` takes one.
* `exitWhenIdle(ms)` is how HP's services quit a few seconds after their last
  command. An open request or subscription keeps the service up.

`kit/db8.ts` — db8, on top of the bus: `find`, `findAll` (every page, as the
loop asks), `get`, `put`, `merge`, `mergeWhere`, `del`, `delWhere`, `batch`,
`putKind`, `delKind`, and `watchFind`, which yields a query's results now and
again after each change (db8's watches fire once; it re-arms them).

`kit/palmbus.ts` — the native addon, typed. Nothing but `luna.ts` uses it.

What luna-service2 needs, found the hard way
--------------------------------------------

* **A role file for the executable**, even with security off, or the hub
  refuses the service name. It matches `/proc/<pid>/exe`: node itself.
* **A `.service` file listing the name**, even for a service that is already
  up, or callers get "Service does not exist".
* **`subscriptionAdd`** for every subscribed request, or the service is never
  told that a subscriber went away.
* Method names are identifiers: `stream-fails` is refused by the URI parser.
* A service does not hear about a subscriber that never cancels, so a
  subscription whose generator fails at once is forgotten on the spot.

Tests
-----

```
components/node-services/test/run.sh            # type check + every test
components/node-services/test/run.sh test/luna.fake.test.ts
WEBOS_TEST_LOGS=build/node-services-logs components/node-services/test/run.sh
```

* `luna-suite.ts` is written once and runs twice: against an in-memory palmbus
  (`luna.fake.test.ts`, runs anywhere) and against a real `ls-hubd`
  (`luna.hub.test.ts`). The real run is what keeps the fake honest.
* `db8.hub.test.ts` runs against a real `mojodb-luna`.
* The hub's sockets have fixed paths under `/tmp`, so `run.sh` gives the run a
  `/tmp` of its own with bwrap, or uses a throwaway container's; anywhere else
  it skips rather than touch a running session.
* Every test was checked by mutation: each behavior above was broken on purpose
  and a test failed.
