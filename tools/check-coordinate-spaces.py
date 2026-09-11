#!/usr/bin/env python3
"""Finds positions handed to the wrong coordinate conversion.

QGraphicsItem has three spaces -- item, parent and scene -- and the conversions
between them take an argument in one specific space:

    mapFromScene(p)    p must be a SCENE position
    mapFromParent(p)   p must be a PARENT position
    mapToScene(p)      p must be an ITEM position

Mixing them compiles, runs, and is silently wrong: the result is a plausible
point in the wrong place. HP's Qt 5 desktop branch had one, and it cost the
whole card-dismiss gesture -- mapFromParent(p.scenePos()) put the finger 955
pixels out on an item 1024 wide, so every move read as outside the card and was
discarded.

That branch never ran on a device, so this sweeps for the same shape elsewhere.

Usage:  check-coordinate-spaces.py <dir>...
Exit 1 if anything is found.
"""
import os
import re
import sys

# (conversion, what its argument must be, what would be wrong in it)
RULES = [
    ("mapFromScene",  "a scene position",
     re.compile(r"(?<![A-Za-z_])(?:pos|lastPos|startPos)\(\)")),
    ("mapToScene",    "an item position",
     re.compile(r"scenePos\(\)")),
    ("mapFromParent", "a position in the item's parent",
     re.compile(r"scenePos\(\)")),
    ("mapToParent",   "an item position",
     re.compile(r"scenePos\(\)")),
    ("mapFromItem",   "a position in the other item",
     re.compile(r"scenePos\(\)")),
]

COMMENT = re.compile(r"^\s*(//|\*|/\*)")


def argument_of(text, call_start):
    """The text between the parentheses of the call starting at call_start."""
    i = text.find("(", call_start)
    if i < 0:
        return None
    depth, j = 0, i
    while j < len(text):
        if text[j] == "(":
            depth += 1
        elif text[j] == ")":
            depth -= 1
            if depth == 0:
                return text[i + 1:j]
        j += 1
    return None


def scan(path):
    findings = []
    lines = open(path, errors="replace").read().splitlines()
    for n, line in enumerate(lines, 1):
        if COMMENT.match(line):
            continue
        for name, expects, wrong in RULES:
            for m in re.finditer(r"(?<![A-Za-z_])%s\s*\(" % name, line):
                arg = argument_of(line, m.start())
                if arg is None:          # call wraps to the next line
                    continue
                bad = wrong.search(arg)
                if bad:
                    findings.append((n, name, expects, bad.group(0), line.strip()))
    return findings


def main(argv):
    roots = argv[1:]
    if not roots:
        print(__doc__)
        return 2
    total = 0
    for root in roots:
        for dirpath, _, files in os.walk(root):
            for fn in sorted(files):
                if not fn.endswith((".cpp", ".h", ".cc")):
                    continue
                path = os.path.join(dirpath, fn)
                for n, name, expects, bad, line in scan(path):
                    total += 1
                    print("%s:%d" % (path, n))
                    print("    %s() wants %s, was given %s" % (name, expects, bad))
                    print("    %s" % line)
    print("%d coordinate-space mismatches" % total)
    print("OK" if total == 0 else "FAIL")
    return 0 if total == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
