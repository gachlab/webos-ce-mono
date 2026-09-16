node-services
=============

webOS services in modern TypeScript (#39). **Ours**, Apache 2.0.

HP's JavaScript services (`components/app-services`) run on a stack built for
node 0.4: MojoLoader, `mojoservice`, `Foundations` futures and HP's `palmbus`
addon, carried by `components/node-v8-shim`. This component is what replaces
that stack, one service at a time, keeping each service's API exactly as it is.
Nothing here uses any of it: the bus is our own addon.

* **No build step.** The pinned node (`tools/node-version`) runs the `.ts`
  files as they are by stripping their types, so the code stays within what
  stripping handles (`erasableSyntaxOnly`). TypeScript 7 (`npm ci`, pinned in the
  root `package.json`) only checks the types.
* **Functional.** No classes in the TypeScript. Every module exposes
  `createX(deps)`, which returns the thing itself; the default wiring is
  exported next to it (`openBus`, `openHandle`). Tests pass fakes through the
  same door.
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

`kit/mojoservice.ts` — what HP's mojoservice did around every command, for
services whose callers still expect it: its failure replies (`errorCode`, or
-9999 with "MojoService: no errorCode supplied " before the text), the 60-second
command timeout, the 5-second idle exit, and `__quit` (which answers and exits
100 ms later, or the answer is lost). `registerCommands` puts a service's
commands on the private bus and the public ones on the public bus too; both
buses share one `Activity`, so neither exits while the other is busy.

`kit/json-schema.ts` — the early JSON Schema dialect Foundations'
`Json.Schema.validate` implemented (a property is required unless
`"optional": true`), limited to the keywords HP's schemas use. Anything else is
refused rather than passed.

`kit/handle.ts` — what `luna.ts` needs from the bus underneath (`OpenHandle`,
`BusHandle`, `BusMessage`). `kit/lunabus.ts` provides it over the addon; the
tests provide an in-memory one.

The addon
---------

`native/lunabus.cpp` builds `lunabus.node` (C++20, Node-API only, so it loads
on later node releases without a rebuild), installed next to HP's addons in
`/usr/palm/nodejs`. `tools/build.sh` builds it in the node addons stage.

* **The loop.** luna-service2 runs on a glib main context and node on libuv.
  The context is driven from libuv: `uv_prepare` runs glib's prepare and query
  and keeps one `uv_poll` per descriptor plus a timer for glib's timeout;
  `uv_check` runs check and dispatch. None of that keeps node alive; an open
  bus handle does. A script that closes its handles ends; a service keeps
  running. (palmbus kept every process alive for good.)
* **Messages are data.** Each arrives as a plain object with its fields read
  once, by kind: a request has a sender and an application id, a reply does
  not, and luna-service2 crashes when asked for a sender a hub-made reply lacks.
* **No unregistering inside a dispatch.** JavaScript runs inside
  luna-service2's callbacks (microtasks drain at the end of each), and a handle
  closed there is unregistered only once glib's dispatch returns. Doing it on
  the spot freed what luna-service2 was still using.
* **Polls are dropped before an unregister.** The next handle may get the same
  descriptor numbers, and a poll left on a closed descriptor never hears the
  new socket.
* **Not covered by a test:** the timer that follows glib's timeout. It is
  glib's contract for a foreign loop, but luna-service2's client side adds no
  timed or idle sources, so nothing on the bus exercises it.

The services
------------

Each lives in `services/<name>/`, answers under HP's name, and takes over from
HP's JavaScript service when the rootfs is assembled: `tools/assemble-rootfs.sh`
installs the kit and the services under `/usr/palm/node-services` and points the
service's `.service` file at `node …/main.ts`. HP's directory stays installed,
because its db8 kinds, permissions and role still come from there.

### com.palm.service.accounts

The accounts service Synergy is built on: templates, accounts, credentials, and
the calls that tell transports about them. All 19 of HP's commands, with the
same names, parameters, replies and side effects.

* `accounts.ts`: the pure part (weaving a template into an account,
  permissions, the public whitelist).
* `templates.ts`: templates read from `/usr/palm/public/accounts` in their
  localized version, as `Globalization.ResourceBundle` looked them up, validated
  with HP's schemas (`schemas/`, copied from HP's service) and sorted by name.
* `credentials.ts`: credentials in db8, as HP's desktop model stored them.
* `commands.ts`: the commands. `service.ts` wires them; `main.ts` is what the
  hub starts.

Deliberate differences from HP's code, both in the transport notifications:

* A transport callback that fails stops the sequence and fails the command.
  HP's left the command waiting for its hour-long timeout, which kept the
  service up for that hour; what happened to the account is the same.
* Each `onEnabled` call carries its own `capabilityProviderId`. HP's shared one
  parameter object between the calls, so they all carried the last one.

Kept as HP had it, on purpose: a validator that fails is only logged (the
account is still made, without credentials); new credentials are announced to
every provider, including the ones being enabled at the same time.

Not covered on the bus: the application id of a public caller cannot be set here
(it needs `LSCallFromApplication`), so the whitelisted path of
`listAccountsPublic` and `readCredentialsPublic` is covered by unit tests only.

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

* `luna-suite.ts` is written once and runs twice: against an in-memory bus
  (`luna.fake.test.ts`, runs anywhere) and against a real `ls-hubd` through the
  addon (`luna.hub.test.ts`). The real run is what keeps the fake honest.
* `lunabus.hub.test.ts` covers the addon's own rules: a script ends once its
  handles close and not before, descriptor reuse, closing inside a callback.
* `db8.hub.test.ts` runs against a real `mojodb-luna`.
* `accounts.unit.test.ts` covers the accounts service's pure parts and the
  schema validator; `accounts.hub.test.ts` runs HP's own test cases
  (`tests/accounts-test.js`) and the rest of the commands against a real hub and
  db8, with the transports, validator and system service faked on the bus;
  `accounts.main.test.ts` starts `main.ts` as its own process.
* The hub's sockets have fixed paths under `/tmp`, so `run.sh` gives the run a
  `/tmp` of its own with bwrap, or uses a throwaway container's; anywhere else
  it skips rather than touch a running session. The bwrap run has its own pid
  namespace as well, so a run that dies takes its hubs and db8 with it.
* No `--test-force-exit`: a test file that does not end on its own has left
  something open.
* Every test was checked by mutation: each behavior above was broken on purpose
  (in the addon too, rebuilding it each time) and a test failed.
