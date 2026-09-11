#!/usr/bin/env python3
"""Finds items that handle touch events they can never receive.

QGraphicsScene only delivers touch to an item that asked for it with
setAcceptTouchEvents(true). Without that call the item's sceneEvent never sees a
TouchBegin, and a handler written for one is dead -- it compiles, it reads as
live code, and it never runs.

OverlayWindowManager is exactly this: its sceneEvent routes TouchBegin, TouchEnd,
TouchCancel and TouchUpdate to handlers that cannot be reached, because neither
it nor WindowManagerBase ever opts in. Everything it does receive comes from
WindowServerLuna's filter chain instead, which is a different coordinate space --
and that is how "y -= 50" ended up standing in for a coordinate conversion.

Usage:  check-touch-opt-in.py <dir>...
Exit 1 if anything not on the KNOWN list is found.
"""
import os
import re
import sys

# Every "Class::method(" on a line that starts a definition. Anchoring with ^\w
# eats the first character, which silently renamed every constructor --
# QmlSceneItem::QmlSceneItem became "mlSceneItem" and stopped matching the
# sceneEvent above it, so classes that do opt in were reported as if they did
# not. Match the qualifier itself and check what precedes it instead.
QUALIFIED = re.compile(r"(\w+)::~?\w+\s*\(")
DECL = re.compile(r"^\s*class\s+(\w+)\s*:\s*public\s+(\w+)")

# Only a QGraphicsItem has to opt in. A QGraphicsView handles touch in
# viewportEvent and a QGestureRecognizer is not an item at all, so both would
# otherwise show up as false positives -- and did.
ITEM_ROOTS = {"QGraphicsItem", "QGraphicsObject", "QGraphicsWidget"}


def inheritance(roots):
    """class -> immediate base, over every header in the tree."""
    bases = {}
    for root in roots:
        for dirpath, _, files in os.walk(root):
            for fn in files:
                if not fn.endswith((".h", ".hpp")):
                    continue
                for line in open(os.path.join(dirpath, fn), errors="replace"):
                    m = DECL.match(line)
                    if m:
                        bases.setdefault(m.group(1), m.group(2))
    return bases


def is_item(cls, bases, seen=None):
    seen = seen or set()
    while cls and cls not in seen:
        if cls in ITEM_ROOTS:
            return True
        seen.add(cls)
        cls = bases.get(cls)
    return False

KNOWN = {
    "OverlayWindowManager":
        "REAL, not yet fixed. Its sceneEvent touch branch is unreachable; the "
        "live path is WindowServerLuna::sysmgrEventFilters. Opting in would "
        "change which items get the touch, so it goes with the coordinate fix "
        "rather than before it. See KNOWN_BUGS.md.",
    "GraphicsItemContainer":
        "Its sceneEvent only forwards touch to a child window and returns the "
        "base result; nothing depends on it being reached.",
}


def classes_in(path):
    """Which classes define methods in this file, and what they call."""
    text = open(path, errors="replace").read()
    lines = text.splitlines()
    handles, opts_in = set(), set()
    current = None
    for line in lines:
        if line[:1] not in (" ", "\t", "", "#", "/"):
            for m in QUALIFIED.finditer(line):
                before = line[:m.start()]
                if before == "" or before[-1] in " \t*&":
                    current = m.group(1)
                    break
        if current is None:
            continue
        if "QEvent::TouchBegin" in line:
            handles.add(current)
        stripped = line.strip()
        if stripped.startswith("//") or stripped.startswith("*"):
            continue
        if "setAcceptTouchEvents" in line and "false" not in line:
            opts_in.add(current)
    return handles, opts_in


def main(argv):
    argv = argv[1:]
    show_known = "--all" in argv
    roots = [a for a in argv if a != "--all"]
    if not roots:
        print(__doc__)
        return 2

    bases = inheritance(roots)
    handles, opts_in = {}, set()
    for root in roots:
        for dirpath, _, files in os.walk(root):
            for fn in sorted(files):
                if not fn.endswith((".cpp", ".h")):
                    continue
                path = os.path.join(dirpath, fn)
                h, o = classes_in(path)
                for c in h:
                    handles.setdefault(c, path)
                opts_in |= o

    new = known = 0
    for cls in sorted(handles):
        if cls in opts_in or not is_item(cls, bases):
            continue
        reason = KNOWN.get(cls)
        if reason:
            known += 1
            if show_known:
                print("%s: %s  (known)" % (handles[cls], cls))
                print("    %s" % reason)
            continue
        new += 1
        print("%s" % handles[cls])
        print("    %s routes TouchBegin but never calls setAcceptTouchEvents,"
              " so it cannot receive one" % cls)
    print("%d never opt in, %d known" % (new, known))
    print("OK" if new == 0 else "FAIL")
    return 0 if new == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
