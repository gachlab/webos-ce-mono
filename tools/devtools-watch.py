#!/usr/bin/env /usr/bin/python3
"""Watch every page in the running shell: console, exceptions and failed loads.

    devtools-watch.py [seconds]

devtools.py asks a page a question once it is up. This attaches to the browser
endpoint instead and follows every target from the moment it is created, with
the page held at the debugger until the listeners are in place -- so what an app
does while it is starting up is recorded too, which an evaluate after the fact
cannot see. That is where the interesting failures are: an exception inside a
setTimeout callback stops the chain and leaves nothing behind to ask about.
"""
import json
import sys
import time
import urllib.request

from websockets.sync.client import connect

PORT = 9222


def browser_ws():
    with urllib.request.urlopen("http://127.0.0.1:%d/json/version" % PORT, timeout=5) as r:
        return json.load(r)["webSocketDebuggerUrl"]


def short(url):
    if not url:
        return "?"
    url = url.split("?")[0].rstrip("/")
    parts = [p for p in url.split("/") if p]
    return "/".join(parts[-2:]) if len(parts) >= 2 else url


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 60.0
    next_id = [0]
    names = {}

    def send(ws, method, params=None, session=None):
        next_id[0] += 1
        msg = {"id": next_id[0], "method": method, "params": params or {}}
        if session:
            msg["sessionId"] = session
        ws.send(json.dumps(msg))

    start = time.time()
    # A target can be attached twice -- once because it was discovered, once
    # because auto-attach caught it being created -- and then every message
    # arrives in duplicate. Keep one session per target and ignore the rest.
    session_of_target = {}
    ignored = set()
    # Log.enable replays whatever the page has already logged, so the first
    # entries after an attach are usually history, not news. Entries that close
    # to the attach are marked [old]: a heuristic, but without it a page's whole
    # past reads as if it were happening now.
    attached_at = {}
    REPLAY_WINDOW = 0.5

    def stamp():
        return "%7.3f" % (time.time() - start)

    def age(session):
        return "[old] " if time.time() - attached_at.get(session, 0) < REPLAY_WINDOW else ""

    with connect(browser_ws(), max_size=None, open_timeout=10) as ws:
        send(ws, "Target.setDiscoverTargets", {"discover": True})
        send(ws, "Target.setAutoAttach",
             {"autoAttach": True, "waitForDebuggerOnStart": True, "flatten": True})

        while time.time() - start < seconds:
            try:
                raw = ws.recv(timeout=max(0.2, seconds - (time.time() - start)))
            except TimeoutError:
                break
            msg = json.loads(raw)
            method = msg.get("method")
            params = msg.get("params", {})
            session = msg.get("sessionId")
            if session in ignored:
                continue
            who = age(session) + names.get(session, session or "-")

            if method == "Target.targetCreated":
                # setAutoAttach only covers targets opened from now on; the pages
                # already up have to be attached by hand or they stay silent.
                info = params.get("targetInfo", {})
                if (info.get("type") == "page" and not info.get("attached")
                        and info["targetId"] not in session_of_target):
                    send(ws, "Target.attachToTarget",
                         {"targetId": info["targetId"], "flatten": True})

            elif method == "Target.attachedToTarget":
                s = params["sessionId"]
                info = params.get("targetInfo", {})
                # A target held at the debugger has not navigated yet, so its
                # targetInfo carries no URL and every line about it read "?".
                # Ask the target list, which does know.
                url = info.get("url")
                if not url:
                    try:
                        for t in json.load(urllib.request.urlopen(
                                "http://127.0.0.1:%d/json" % PORT, timeout=3)):
                            if t.get("id") == info.get("targetId"):
                                url = t.get("url")
                                break
                    except Exception:
                        pass
                tid = info.get("targetId")
                if tid in session_of_target:
                    # Same page, second attach: leave it silent rather than
                    # report everything twice.
                    ignored.add(s)
                    continue
                session_of_target[tid] = s
                attached_at[s] = time.time()
                names[s] = short(url) if url else (tid or "?")[:8]
                print("%s ATTACH %s" % (stamp(), names[s]), flush=True)
                send(ws, "Runtime.enable", session=s)
                send(ws, "Log.enable", session=s)
                send(ws, "Runtime.runIfWaitingForDebugger", session=s)

            elif method == "Runtime.exceptionThrown":
                d = params.get("exceptionDetails", {})
                text = d.get("text", "")
                exc = d.get("exception") or {}
                desc = exc.get("description") or exc.get("value") or ""
                print("%s EXCEPTION [%s] %s %s" % (stamp(), who, text, str(desc)[:600]),
                      flush=True)

            elif method == "Log.entryAdded":
                e = params.get("entry", {})
                if e.get("level") in ("error", "warning"):
                    # The whole URL, not a shortened one: two different files can
                    # end in build/enyo-build.js and only the full path says
                    # which of them is missing.
                    print("%s %-7s [%s] %s %s" % (stamp(), e.get("level", "").upper(), who,
                                                  e.get("text", "")[:300],
                                                  e.get("url") or ""),
                          flush=True)

            elif method == "Runtime.consoleAPICalled":
                kind = params.get("type")
                if kind in ("error", "warning", "assert"):
                    args = " ".join(str(a.get("value", a.get("description", "")))[:200]
                                    for a in params.get("args", []))
                    print("%s CONSOLE.%s [%s] %s" % (stamp(), kind, who, args), flush=True)

    return 0


if __name__ == "__main__":
    sys.exit(main())
