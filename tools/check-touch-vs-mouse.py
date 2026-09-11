#!/usr/bin/env python3
"""Compares each Qt 5 touch handler with the mouse handler it was adapted from.

HP's Qt 5 desktop branch is, almost everywhere, a touch twin of a mouse handler
that had been shipping for years. The twin was written by copying the mouse one
and swapping the accessor, and where the swap was not exact the result compiles,
runs, and points somewhere plausible but wrong. Three separate bugs in this tree
came out of that:

  * a scene position passed to mapFromParent, which killed the card-dismiss
    gesture outright
  * an else-if repeating an earlier condition, so drag updates in the launcher
    overlay were unreachable
  * scenePos() where the mouse twin used pos(), papered over afterwards by
    subtracting a hard-coded 50 from y instead of converting

So: for every class with both, report which position accessor each side feeds
into the Event it builds. A mismatch is not proof of a bug -- the two may arrive
by different delivery paths -- but every one of them is worth a look, and
tests/touch-coordinate-spaces-qt5.cpp says what the answer depends on.

Usage:  check-touch-vs-mouse.py <dir>...
Exit 1 if a mismatch is not on the KNOWN list.
"""
import os
import re
import sys

TOUCH = re.compile(r"^\w[\w\s:<>*&]*?(\w+)::(handleTouch\w*)\s*\(")
MOUSE = re.compile(r"^\w[\w\s:<>*&]*?(\w+)::(mouse\w*Event|handleMouseEvent)\s*\(")
ACCESSOR = re.compile(r"(?:->|\.)(scenePos|pos|lastPos|startPos|screenPos|windowPos)\s*\(\)")

# Reviewed. Keyed by class and the pair of accessors.
KNOWN = {
    ("OverlayWindowManager", "scenePos", "pos"):
        "REAL, not yet fixed. handleTouch* use scenePos() where the mouse twin "
        "uses pos(), and mapCoordToWindow's Qt 5 branch then replaces the real "
        "conversion with 'y -= 50'. Left alone because these handlers are fed "
        "from two delivery paths at once -- OverlayWindowManager::sceneEvent "
        "and WindowServerLuna::sysmgrEventFilters -- and the same accessor "
        "means different things in each, so it cannot be fixed by swapping the "
        "accessor. See KNOWN_BUGS.md.",
}


def body_of(lines, i):
    depth, started, out = 0, False, []
    j = i
    while j < len(lines) and j < i + 200:
        depth += lines[j].count("{") - lines[j].count("}")
        if "{" in lines[j]:
            started = True
        out.append(lines[j])
        if started and depth <= 0:
            break
        j += 1
    return out


def accessors_in(body):
    found = []
    for line in body:
        if re.match(r"^\s*(//|\*)", line):
            continue
        if "ev.x" in line or "ev.y" in line or re.search(r"\bqreal\s+[xy]\s*=", line) \
           or re.search(r"QPointF\s+\w+\s*=", line):
            for m in ACCESSOR.finditer(line):
                found.append(m.group(1))
    return found


def scan(path):
    lines = open(path, errors="replace").read().splitlines()
    touch, mouse = {}, {}
    for i, line in enumerate(lines):
        m = TOUCH.match(line)
        if m:
            touch.setdefault(m.group(1), []).extend(accessors_in(body_of(lines, i)))
            continue
        m = MOUSE.match(line)
        if m:
            mouse.setdefault(m.group(1), []).extend(accessors_in(body_of(lines, i)))
    out = []
    for cls in sorted(set(touch) & set(mouse)):
        t, s = sorted(set(touch[cls])), sorted(set(mouse[cls]))
        if t and s and t != s:
            out.append((cls, t, s))
    return out


def main(argv):
    argv = argv[1:]
    show_known = "--all" in argv
    roots = [a for a in argv if a != "--all"]
    if not roots:
        print(__doc__)
        return 2
    new = known = 0
    for root in roots:
        for dirpath, _, files in os.walk(root):
            for fn in sorted(files):
                if not fn.endswith(".cpp"):
                    continue
                path = os.path.join(dirpath, fn)
                for cls, t, s in scan(path):
                    key = (cls, ",".join(t), ",".join(s))
                    reason = None
                    for (kcls, kt, ks), r in KNOWN.items():
                        if kcls == cls and kt in t and ks in s:
                            reason = r
                    if reason:
                        known += 1
                        if show_known:
                            print("%s: %s  touch uses %s, mouse uses %s  (known)"
                                  % (path, cls, "/".join(t), "/".join(s)))
                            print("    %s" % reason)
                        continue
                    new += 1
                    print("%s" % path)
                    print("    %s: touch handlers use %s, the mouse twin uses %s"
                          % (cls, "/".join(t), "/".join(s)))
    print("%d new mismatches, %d known" % (new, known))
    print("OK" if new == 0 else "FAIL")
    return 0 if new == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
