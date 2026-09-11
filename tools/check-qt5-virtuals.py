#!/usr/bin/env python3
"""Finds virtual functions declared only in a Qt5 branch that override nothing.

HP ported luna-sysmgr to Qt5 with #if QT_VERSION guards everywhere, but the
port was never finished and some Qt5 branches had never run. One of them was:

    #if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
        virtual QDeclarativeEngine* declarativeEngine() { return m_qmlEngine; }
    #else
        virtual QQmlEngine*         declarativeEngine() { return m_qmlEngine; }
    #endif

The Qt5 virtual on the base class is called qmlEngine(), not
declarativeEngine(). HP copied the Qt4 branch, changed the type and left the
old name, so this overrode nothing and qmlEngine() kept returning the base
class's NULL -- even though the engine is created fine in the constructor.
That cost a crash (a null dashboard container) and a notification with no text.

The compiler cannot warn about this: under Qt5 the base simply has no
declarativeEngine(), so the derived one is just a new method, not a hidden
override.

How it works: it reconstructs what the compiler sees under Qt5 (unguarded code
plus the #else branches), collects every virtual declared there, and reports
the ones whose name appears in a single file. A virtual that nothing else
declares cannot be overriding anything.

It decides nothing: it raises candidates to look at by hand.

Usage:  tools/check-qt5-virtuals.py components/luna-sysmgr/Src [...]
"""
import re, sys, pathlib, collections

GUARD = re.compile(r'#\s*if\s*\(?\s*QT_VERSION\s*<\s*QT_VERSION_CHECK\s*\(\s*5')
ELSE  = re.compile(r'#\s*else')
ENDIF = re.compile(r'#\s*endif')
VIRT  = re.compile(r'\bvirtual\b[^;{(]*?\b([A-Za-z_]\w*)\s*\(')


def qt5_lines(path):
    """(line_no, text, inside_qt5_branch) for what the compiler sees on Qt5."""
    try:
        lines = path.read_text(errors='replace').splitlines()
    except OSError:
        return []
    out, i = [], 0
    while i < len(lines):
        if GUARD.search(lines[i]):
            branch, depth = 4, 0
            i += 1
            while i < len(lines):
                line = lines[i]
                if re.match(r'\s*#\s*if', line):
                    depth += 1
                elif ENDIF.search(line):
                    if depth == 0:
                        break
                    depth -= 1
                elif ELSE.search(line) and depth == 0:
                    branch = 5
                    i += 1
                    continue
                if branch == 5:
                    out.append((i + 1, line, True))
                i += 1
        else:
            out.append((i + 1, lines[i], False))
        i += 1
    return out


def main(roots):
    files = []
    for root in roots:
        files += [p for p in pathlib.Path(root).rglob('*')
                  if p.suffix in ('.h', '.hpp', '.cpp')]

    seen_in = collections.defaultdict(set)
    for path in files:
        for _, line, _ in qt5_lines(path):
            for m in VIRT.finditer(line):
                seen_in[m.group(1)].add(path)

    found = 0
    for path in files:
        for line_no, line, in_qt5 in qt5_lines(path):
            if not in_qt5:
                continue
            for m in VIRT.finditer(line):
                name = m.group(1)
                if len(seen_in[name]) == 1:
                    print(f"  {path}:{line_no} -> {name}()")
                    print(f"      {line.strip()[:80]}")
                    found += 1
    print(f"\n{found} candidates")
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:] or ['components/luna-sysmgr/Src']))
