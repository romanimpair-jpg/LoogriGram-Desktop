"""Check brace and paren balance of changed files, ignoring strings/comments.

    python3 Telegram/build/audit_balance.py [BASE]

BASE defaults to HEAD. Every changed .cpp/.h/.style must balance to exactly
zero once comments and string/char literals are stripped; anything else is
reported.

Why not just count characters: the raw counts in this tree are non-zero for
several files, because braces and parens appear inside string literals and
inside commented-out code. Comparing the raw count against the base only
tells you the number moved, not whether the code is broken - a scripted edit
that eats a real brace while deleting a comment containing one nets out to
"unchanged". Stripping first makes the answer absolute.

Gotcha this already tripped over: a C++ digit separator (2'000) is not the
start of a character literal, so a leading alphanumeric means "skip".

Two files are known exceptions, listed below. Both are byte-identical on
pristine upstream `dev`, on the last green build and on HEAD, so whatever
construct the stripper mishandles there is upstream's and is not something
the fork did. They are skipped rather than tolerated as "close enough": if
one of them ever needs editing, drop it from the list for that change and
read the seam by hand instead.
"""

KNOWN_UPSTREAM_EXCEPTIONS = {
    # stripper reports paren -1 brace -1; unchanged since upstream.
    'Telegram/SourceFiles/history/view/history_view_context_menu.cpp',
    # stripper reports brace +1; unchanged since upstream.
    'Telegram/SourceFiles/settings/sections/settings_premium.cpp',
}
import io
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BS = chr(92)


def strip_code(text):
    """Remove comments and string/char literals, keep everything else."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ''
        if c == '/' and nxt == '/':
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif c == '/' and nxt == '*':
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
        elif c == '"':
            i += 1
            while i < n:
                if text[i] == BS:
                    i += 2
                    continue
                if text[i] == '"':
                    i += 1
                    break
                i += 1
        elif c == "'" and not (out and (out[-1].isalnum() or out[-1] == '_')):
            i += 1
            while i < n:
                if text[i] == BS:
                    i += 2
                    continue
                if text[i] == "'":
                    i += 1
                    break
                i += 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def main():
    base = sys.argv[1] if len(sys.argv) > 1 else 'HEAD'
    changed = subprocess.run(
        ['git', '-C', ROOT, 'diff', '--name-only', base],
        capture_output=True, text=True).stdout.split()
    bad = 0
    skipped = 0
    for f in changed:
        if not f.endswith(('.cpp', '.h', '.style')):
            continue
        if f in KNOWN_UPSTREAM_EXCEPTIONS:
            skipped += 1
            continue
        full = os.path.join(ROOT, f.replace('/', os.sep))
        if not os.path.exists(full):
            continue
        new = strip_code(
            io.open(full, encoding='utf-8', errors='replace', newline='').read())
        paren = new.count('(') - new.count(')')
        brace = new.count('{') - new.count('}')
        if paren or brace:
            bad += 1
            print('%-70s paren %+d  brace %+d' % (f, paren, brace))
    if skipped:
        print('%d known upstream exception(s) skipped' % skipped)
    print('%d file(s) out of balance' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
