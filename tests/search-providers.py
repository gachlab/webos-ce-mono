#!/usr/bin/env python3
"""The search providers the browser and Just Type offer (#9).

Checks the list tools/assemble-rootfs.sh installs, after the same "loc_"
conversion: every URL is HTTPS, every enabled provider can suggest, each URL
takes the search terms, and a changed entry carries a version above HP's, so
the service replaces what an install already stored.

The endpoints themselves were checked by hand when chosen (curl, 2026-09-17);
this does not reach the network.
"""
import json
import pathlib
import sys

LIST = pathlib.Path(__file__).resolve().parent.parent / \
    "components/luna-universalsearchmgr/desktop-support/UniversalSearchList.json"


def localized(value):
    if isinstance(value, dict):
        return {(k[4:] if k.startswith("loc_") else k): localized(v) for k, v in value.items()}
    if isinstance(value, list):
        return [localized(v) for v in value]
    return value


failures = 0


def check(what, ok):
    global failures
    print(f"  {what:60} {'OK' if ok else '<-- FAIL'}")
    failures += 0 if ok else 1


providers = localized(json.loads(LIST.read_text()))["UniversalSearchList"]
check("the list has providers", len(providers) > 0)
for p in providers:
    name = p.get("id", "?")
    for key in ("url", "suggestURL"):
        if key in p:
            check(f"{name}: {key} is HTTPS", p[key].startswith("https://"))
            check(f"{name}: {key} takes the search terms", "#{searchTerms}" in p[key])
    check(f"{name}: has a search URL", "url" in p)
    if p.get("enabled"):
        check(f"{name}: can suggest", "suggestURL" in p)
    check(f"{name}: is no longer Twitter", "twitter" not in json.dumps(p).lower())
    check(f"{name}: is replaced over what an install stored", p.get("version", 1) >= 2)

sys.exit(1 if failures else 0)
