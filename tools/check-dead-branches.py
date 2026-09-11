#!/usr/bin/env python3
"""Finds else-if branches that can never run because an earlier one is identical.

    } else if (e->type() == QEvent::TouchBegin) {
        return handleTouchBegin(e);
    } else if (e->type() == QEvent::TouchEnd) {
        return handleTouchEnd(e);
    } else if (e->type() == QEvent::TouchBegin) {   // never reached
        return handleTouchUpdate(e);
    }

The compiler says nothing, the code looks symmetric, and a whole branch of
behaviour is silently gone -- in that real case, from HP's Qt 5 desktop port,
drag updates in the launcher overlay were never delivered.

Usage:  check-dead-branches.py <dir>...
Exit 1 if anything is found.
"""
import os
import re
import sys

IF = re.compile(r"^(\s*)if\s*\(")
ELSE_IF = re.compile(r"^(\s*)\}?\s*else\s+if\s*\(")


def condition_from(lines, i):
    """The text inside the parentheses of the if on line i, however many lines
    it spans, or None if it does not close within a few."""
    line = lines[i]
    start = line.find("(")
    if start < 0:
        return None
    depth = 0
    out = []
    j = i
    k = start
    while j < len(lines) and j <= i + 10:
        line = lines[j]
        while k < len(line):
            ch = line[k]
            if ch == "(":
                depth += 1
                if depth == 1:
                    k += 1
                    continue
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    return " ".join("".join(out).split())
            out.append(ch)
            k += 1
        out.append(" ")
        j += 1
        k = 0
    return None


def scan(path):
    lines = open(path, errors="replace").read().splitlines()
    findings = []
    chain_indent = None
    seen = {}
    for i, line in enumerate(lines):
        m_elif = ELSE_IF.match(line)
        if m_elif and chain_indent is not None and len(m_elif.group(1)) == chain_indent:
            cond = condition_from(lines, i)
            if cond is None:
                continue
            if cond in seen:
                findings.append((i + 1, seen[cond] + 1, cond))
            else:
                seen[cond] = i
            continue
        m_if = IF.match(line)
        if m_if:
            cond = condition_from(lines, i)
            chain_indent = len(m_if.group(1))
            seen = {cond: i} if cond else {}
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
                for line, first, cond in scan(path):
                    total += 1
                    print("%s:%d" % (path, line))
                    print("    unreachable: the same condition is tested at line %d" % first)
                    print("    %s" % cond)
    print("%d dead branches" % total)
    print("OK" if total == 0 else "FAIL")
    return 0 if total == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
