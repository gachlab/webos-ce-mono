# Community knowledge (webOS Archive)

A copy of the knowledge base from **[webOSArchive/webos-mcp][mcp]**, taken on
2026-09-10. 43 documents, **MIT** licensed, copyright (c) 2026 webOS Archive
(see `LICENSE`). **Not HP code** and not part of the stack: it is here as
reference.

The original is an MCP server, meant to give Claude this knowledge in every
session without having to re-explain it. What is here is only the text; for the
full route, `npx webos-mcp` plus a `CLAUDE.md` that loads it.

## What it covers, and what it does not

It covers **writing apps** for Palm/HP webOS (2009-2012): Mojo, Enyo, the bus,
db8, packaging, the PDK. It explicitly does not cover LG's webOS.

**It does not cover building the OS from source**, which is what this
repository does. So it is complementary, not overlapping.

## What it has already been useful for

- `gotchas.md` -> "Always cancel subscriptions you no longer need or they will
  keep the service running". That backs implementing `cancel()` in
  `PalmServiceBridgeAdapter` even though instrumenting the bridge never caught
  an app calling it.
- `gotchas.md` -> do not call a privileged service from an app whose id does not
  start with `com.palm`. That is exactly the `callerId` our adapter passes.
- `tls-and-networking.md` -> how they solved modern TLS on the old stack. Prior
  art for when the browser has to reach the internet.

## Context

It comes from the same group that released **webOS CE 3.1.0** (2026-09-03), a
binary Doctor for the TouchPad: HP's 3.0.5 rootfs repacked with 14 years of
community work on top, including LunaCE and OpenSSL 1.1.1w. **Nothing is
rebuilt** for that release. It runs on a different track from this repository,
which builds HP's original code on modern Linux.

[mcp]: https://github.com/webOSArchive/webos-mcp
