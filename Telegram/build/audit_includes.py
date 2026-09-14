"""Find includes removed from surviving files whose symbols are still used.

This is the class of bug that broke g211f468: nothing in unread_badge.cpp
changed, but two headers it had been getting transitively went away with the
includes that supplied them, and it stopped compiling.

Two things keep the noise down. Hunks of a deleted file are attributed to
/dev/null rather than to whichever file the diff happened to print before it.
And a name only counts if no *other* header the file still includes declares
it too - otherwise every mention of Session or Element is a false alarm.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "Telegram/SourceFiles")

RANGE = sys.argv[1] if len(sys.argv) > 1 else "HEAD~1..HEAD"

diff = subprocess.run(
    ["git", "diff", RANGE, "--", "*.cpp", "*.h"],
    cwd=ROOT, capture_output=True, text=True).stdout

current = None
removed = []
for line in diff.split("\n"):
    if line.startswith("+++ "):
        target = line[4:].strip()
        current = None if target == "/dev/null" else target[2:]
    elif line.startswith("-#include") and current:
        m = re.match(r'-#include ["<]([^">]+)[">]', line)
        if m:
            removed.append((current, m.group(1)))

DECL = re.compile(
    r'^(?:class|struct|enum class|enum)\s+(\w+)'
    r'|^\[\[nodiscard\]\]\s+[\w:<>,\s*&]+?\b(\w+)\s*\('
    r'|^[\w:<>,\s*&]+?\b([A-Z]\w+)\s*\(', re.M)


def declared(header_path):
    try:
        text = open(header_path, encoding="utf-8", errors="ignore").read()
    except OSError:
        return set()
    names = set()
    for m in DECL.finditer(text):
        name = m.group(1) or m.group(2) or m.group(3)
        if name and len(name) > 3 and name[0].isupper():
            names.add(name)
    return names


def current_includes(body):
    return [m.group(1) for m in re.finditer(r'#include "([^"]+)"', body)]


suspects = []
for path, header in removed:
    full = os.path.join(ROOT, path)
    hpath = os.path.join(SRC, header)
    if not os.path.exists(full) or not os.path.exists(hpath):
        continue
    body = open(full, encoding="utf-8", errors="ignore").read()
    names = declared(hpath)
    if not names:
        continue
    # Anything the remaining includes also declare is not evidence.
    elsewhere = set()
    for other in current_includes(body):
        op = os.path.join(SRC, other)
        if os.path.exists(op):
            elsewhere |= declared(op)
    unique = names - elsewhere
    hits = sorted(n for n in unique
                  if re.search(r'\b%s\b' % re.escape(n), body))
    if hits:
        suspects.append((path, header, hits[:8]))

for path, header, hits in suspects:
    print("  %s" % path.replace("Telegram/SourceFiles/", ""))
    print("      dropped %s" % header)
    print("      only that header declares: %s" % ", ".join(hits))
print("\n%d suspect(s) to read by hand" % len(suspects))
