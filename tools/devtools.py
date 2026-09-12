#!/usr/bin/env /usr/bin/python3
"""Ask a page inside the running shell a question.

    devtools.py <url-fragment> '<javascript expression>'
    devtools.py --list

WebAppMgr opens the DevTools server when the shell is started with
WEBOS_WAM_INSPECTOR=<port>; every card, dashboard and headless page is a target.
"""
import json
import sys
import urllib.request

from websockets.sync.client import connect

PORT = 9222


def targets():
    with urllib.request.urlopen("http://127.0.0.1:%d/json" % PORT, timeout=5) as r:
        return json.load(r)


def evaluate(ws_url, expression):
    with connect(ws_url, max_size=None, open_timeout=10) as ws:
        ws.send(json.dumps({
            "id": 1,
            "method": "Runtime.evaluate",
            "params": {"expression": expression, "returnByValue": True, "awaitPromise": True},
        }))
        while True:
            message = json.loads(ws.recv(timeout=15))
            if message.get("id") == 1:
                return message


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--list":
        for t in targets():
            print("%-6s %s" % (t.get("type"), t.get("url")))
        return 0

    if len(sys.argv) < 3:
        print(__doc__)
        return 2

    fragment, expression = sys.argv[1], sys.argv[2]
    matches = [t for t in targets() if fragment in (t.get("url") or "")]
    if not matches:
        print("no target whose url contains %r" % fragment)
        for t in targets():
            print("  have: %s" % t.get("url"))
        return 1
    if len(matches) > 1:
        print("note: %d targets match; using the first" % len(matches))

    reply = evaluate(matches[0]["webSocketDebuggerUrl"], expression)
    result = reply.get("result", {})
    if "exceptionDetails" in result:
        print("exception:", json.dumps(result["exceptionDetails"])[:400])
        return 1
    value = result.get("result", {}).get("value")
    print(value if isinstance(value, str) else json.dumps(value, indent=1)[:4000])
    return 0


if __name__ == "__main__":
    sys.exit(main())
