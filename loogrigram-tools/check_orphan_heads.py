"""Find declaration heads orphaned by a removal.

remove() in removal_defs.py cuts a definition from the line its anchor is
on. When the anchor was the name line, a `template <...>` line or a return
type on the line above survives and glues itself to whatever follows:

    [[nodiscard]] std::optional<Foo>      <- left behind
    class Bar final : public Baz {        <- C2236 unexpected token 'class'

This walks `git diff BASE HEAD` for C++ files and reports, for every run of
removed lines, a kept line right above it that looks like the head of a
declaration: a template line, or a line that does not end a statement.
Usage (from the repository root):
    python3 tools/check_orphan_heads.py <base-commit>
"""
import re
import subprocess
import sys

TEMPLATE = re.compile(r'^\s*template\s*<.*>\s*$')
# A kept line that ends mid-declaration: not ; { } , ) : and not a
# comment, preprocessor line, label or blank.
OPEN_ENDED = re.compile(r'^\s*(\[\[nodiscard\]\]\s*)?[A-Za-z_][\w:<>,\s\*&]*[\w>\*&]\s*$')
SKIP = re.compile(r'^\s*($|//|/\*|\*|#|public:|private:|protected:|case |default:)')
KEYWORDS = {'else', 'return', 'break', 'continue', 'do', 'try', 'public',
            'private', 'protected', 'namespace'}


def suspicious(line):
    if TEMPLATE.match(line):
        return True
    if SKIP.match(line):
        return False
    stripped = line.strip()
    if stripped.split(' ')[0] in KEYWORDS:
        return False
    return bool(OPEN_ENDED.match(line))


def main():
    base = sys.argv[1]
    diff = subprocess.run(
        ['git', 'diff', '-U3', base, 'HEAD', '--', '*.cpp', '*.h'],
        capture_output=True, text=True, encoding='utf-8', errors='replace',
        check=True).stdout
    path = None
    new_line = 0
    last_kept = None
    last_kept_no = 0
    in_removal = False
    found = 0
    for raw in diff.splitlines():
        if raw.startswith('+++ '):
            path = raw[6:] if raw.startswith('+++ b/') else None
            continue
        if raw.startswith('--- ') or raw.startswith('diff '):
            continue
        m = re.match(r'^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@', raw)
        if m:
            new_line = int(m.group(1))
            last_kept = None
            in_removal = False
            continue
        if path is None:
            continue
        if raw.startswith('-'):
            if not in_removal and last_kept is not None:
                if suspicious(last_kept):
                    print('%s:%d: %s' % (path, last_kept_no, last_kept.strip()))
                    found += 1
            in_removal = True
            continue
        if raw.startswith('+'):
            last_kept = raw[1:]
            last_kept_no = new_line
            new_line += 1
            in_removal = False
            continue
        # context line
        last_kept = raw[1:] if raw.startswith(' ') else raw
        last_kept_no = new_line
        new_line += 1
        in_removal = False
    print('%d suspicious heads' % found)
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main())
