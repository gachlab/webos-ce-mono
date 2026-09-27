#!/usr/bin/env /usr/bin/python3
"""Compare a kit control against its enyo reference, by the numbers.

    tools/kit-ab.py <control> [more controls...]
    tools/kit-ab.py --list

Reads getComputedStyle + getBoundingClientRect off the same control in both the
rewritten card (target titled "Kit") and the enyo reference ("Kit (enyo)"), and
prints them side by side with the differences flagged. This is the exact,
faster half of the A/B described in docs/web-foundation.md: a screenshot says
two controls differ, this says which property and by how much.

Notes:
  * getComputedStyle reads even when a control is scrolled off-screen (enyo's
    Scroller does not honour scrollIntoView, so low controls cannot be shot but
    can still be measured). getBoundingClientRect for those is viewport-relative
    and may be negative -- width/height stay meaningful.
  * Controls painted with border-image (button, picker pill, light toolbar)
    return the token, not the colour, from getComputedStyle. For colour, sample
    the PNG: convert IMG -format '%[pixel:p{x,y}]' info:
  * The webOS session must be up with the inspector on PORT (WEBOS_WAM_INSPECTOR).

The kit side is reached through the shadow root; the enyo side by Onyx class.
Add a control by giving it a row in CONTROLS below: a CSS path into the kit's
shadow DOM and the matching enyo selector.
"""
import json
import sys
import urllib.request

from websockets.sync.client import connect

PORT = 9222
KIT_TITLE = "Kit"
ENYO_TITLE = "Kit (enyo)"

# Properties worth comparing for a control's box and type. Kept small so the
# diff is readable; extend per control if a bug needs more.
PROPS = [
    "font-size", "line-height", "font-weight", "color",
    "padding-top", "padding-bottom", "padding-left", "padding-right",
    "margin-top", "margin-bottom", "min-height", "height",
    "background-color", "background-image", "border-top", "border-bottom",
    "display", "align-items",
]

# control -> (kit host tag, kit shadow selector, enyo selector). The kit side is
# "<host-tag>>>>>" split on ">>>" into the custom element and the inner node.
CONTROLS = {
    # name:            (kit host,            kit inner,               enyo selector)
    "divider-caption": ("wos-divider",       ".wos-divider-caption",  ".enyo-divider-caption"),
    "divider":         ("wos-divider",       ".wos-divider",          ".enyo-divider-caption"),  # enyo has no box; compare caption's item via parent
    "row":             ("wos-row",           ".wos-row",              ".enyo-item"),
    "row-title":       ("wos-row",           ".wos-row-title",        ".enyo-item"),
    "row-detail":      ("wos-row",           ".wos-row-detail",       ".kit-enyo-detail"),
    "picker-pill":     ("wos-picker",        ".wos-picker-pill",      ".enyo-picker-button"),
    "check":           ("wos-check",         ".wos-check",            ".enyo-checkbox"),
    "toggle":          ("wos-toggle",        ".wos-toggle",           ".enyo-toggle-button"),
    "search":          ("wos-search-field",  ".wos-search",           ".enyo-input"),
    "field":           ("wos-field",         ".wos-field-input",      ".enyo-input"),
    "tab":             ("wos-tab-group",     ".wos-tab",              ".enyo-tabbutton"),
    "note":            (None,                ".wos-note",             ".kit-enyo-note"),
}

MEASURE = r"""
(sel, hostTag, inner) => {
  const rect = el => { if(!el) return null; const r=el.getBoundingClientRect();
    return {w:Math.round(r.width), h:Math.round(r.height), top:Math.round(r.top)}; };
  const style = el => { if(!el) return null; const s=getComputedStyle(el);
    const o={}; %s.forEach(p=>o[p]=s.getPropertyValue(p)); return o; };
  let el = null;
  if (hostTag) {
    const host=[...document.querySelectorAll(hostTag)].find(h=>h.shadowRoot&&h.shadowRoot.querySelector(inner));
    el = host ? host.shadowRoot.querySelector(inner) : null;
  } else {
    el = document.querySelector(inner);
  }
  if (!el && sel) el = document.querySelector(sel);
  return {found: !!el, rect: rect(el), style: style(el)};
}
""" % json.dumps(PROPS)


def targets():
    with urllib.request.urlopen("http://127.0.0.1:%d/json" % PORT, timeout=5) as r:
        return json.load(r)


def ws_for(title):
    for t in targets():
        if t.get("title") == title:
            return t["webSocketDebuggerUrl"]
    raise SystemExit("no target titled %r (is the session up?)" % title)


def evaluate(ws_url, expression):
    with connect(ws_url, max_size=None, open_timeout=10) as ws:
        ws.send(json.dumps({
            "id": 1, "method": "Runtime.evaluate",
            "params": {"expression": expression, "returnByValue": True, "awaitPromise": True},
        }))
        while True:
            m = json.loads(ws.recv(timeout=15))
            if m.get("id") == 1:
                r = m.get("result", {})
                if "exceptionDetails" in r:
                    return {"error": json.dumps(r["exceptionDetails"])[:300]}
                return r.get("result", {}).get("value")


def measure(ws_url, kit_side, host, inner, enyo_sel):
    if kit_side:
        call = "(%s)(%s, %s, %s)" % (MEASURE, json.dumps(None), json.dumps(host), json.dumps(inner))
    else:
        call = "(%s)(%s, %s, %s)" % (MEASURE, json.dumps(enyo_sel), json.dumps(None), json.dumps(enyo_sel))
    return evaluate(ws_url, call)


def show(name, kit, enyo):
    print("\n== %s ==" % name)
    if not kit or not enyo:
        print("  measure failed: kit=%r enyo=%r" % (kit, enyo))
        return
    if not kit.get("found"):
        print("  kit: not found")
    if not enyo.get("found"):
        print("  enyo: not found")
    kr, er = kit.get("rect") or {}, enyo.get("rect") or {}
    ks, es = kit.get("style") or {}, enyo.get("style") or {}
    rows = []
    for dim in ("w", "h"):
        w, e = kr.get(dim), er.get(dim)
        rows.append((dim, str(w), str(e), w != e and w is not None and e is not None))
    for p in PROPS:
        w, e = ks.get(p), es.get(p)
        rows.append((p, str(w), str(e), w != e and w is not None and e is not None))
    print("  %-18s %26s   %26s" % ("prop", "kit", "enyo"))
    print("  " + "-" * 74)
    for p, w, e, diff in rows:
        flag = "  <-- diff" if diff else ""
        print("  %-18s %26s   %26s%s" % (p, w[:26], e[:26], flag))


def main():
    if len(sys.argv) < 2 or sys.argv[1] == "--list":
        print(__doc__)
        print("controls:", ", ".join(sorted(CONTROLS)))
        return 0
    kit_ws, enyo_ws = ws_for(KIT_TITLE), ws_for(ENYO_TITLE)
    for name in sys.argv[1:]:
        spec = CONTROLS.get(name)
        if not spec:
            print("unknown control %r; --list to see them" % name)
            continue
        host, inner, enyo_sel = spec
        kit = measure(kit_ws, True, host, inner, enyo_sel)
        enyo = measure(enyo_ws, False, host, inner, enyo_sel)
        show(name, kit, enyo)
    return 0


if __name__ == "__main__":
    sys.exit(main())
