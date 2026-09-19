"""Brace-aware removal helpers for scripted C++ deletions.

Every removal asserts: the anchor is unique, the removed span is balanced,
and the span is exactly one top-level definition.
"""
import re


def read(f):
    return open(f, encoding='utf-8', newline='').read()


def write(f, s):
    open(f, 'w', encoding='utf-8', newline='').write(s)


def _strip(text):
    text = re.sub(r'//[^\n]*', lambda m: ' ' * len(m.group(0)), text)
    text = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text, flags=re.S)
    text = re.sub(r'R"\((.*?)\)"', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text, flags=re.S)
    text = re.sub(r'"(?:\\.|[^"\\\n])*"', lambda m: ' ' * len(m.group(0)), text)
    text = re.sub(r"'(?:\\.|[^'\\\n])'", lambda m: ' ' * len(m.group(0)), text)
    return text


def _span(s, start):
    """From `start` (a definition's first char) to just past its closing brace
    and the newline(s) after it. Handles a trailing `;` for classes/structs."""
    clean = _strip(s)
    i = clean.find('{', start)
    semi = clean.find(';', start)
    if semi != -1 and (i == -1 or semi < i):
        # a declaration, not a definition
        end = semi + 1
    else:
        while True:
            depth = 0
            j = i
            while True:
                c = clean[j]
                if c == '{':
                    depth += 1
                elif c == '}':
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            # A brace initializer in a constructor's init list is followed by
            # another initializer (",") or by the body ("{"); a body is not.
            k = j + 1
            while k < len(clean) and clean[k] in ' \t\r\n':
                k += 1
            if k < len(clean) and clean[k] in ',{)':
                i = clean.find('{', k)
                continue
            break
        end = j + 1
        if end < len(s) and s[end] == ';':
            end += 1
    # swallow the rest of the line and one blank line
    nl = s.find('\n', end)
    end = len(s) if nl == -1 else nl + 1
    if s.startswith('\n', end):
        end += 1
    return end


def remove(f, anchors, text=None):
    """Remove each definition/declaration starting at a line beginning with
    an anchor (exact prefix, at column 0 or with its indentation)."""
    s = read(f) if text is None else text
    for a in anchors:
        pat = re.compile(r'(?m)^' + re.escape(a))
        hits = [m.start() for m in pat.finditer(s)]
        assert len(hits) == 1, (f, a, len(hits))
        start = hits[0]
        end = _span(s, start)
        chunk = _strip(s[start:end])
        assert chunk.count('{') == chunk.count('}'), (f, a, 'unbalanced')
        s = s[:start] + s[end:]
    if text is None:
        write(f, s)
    return s


def lit(f, old, new, n=1):
    s = read(f)
    assert s.count(old) == n, (f, old[:90], s.count(old))
    write(f, s.replace(old, new))


def sub(f, pat, rep, n, flags=0):
    s = read(f)
    s2, k = re.subn(pat, rep, s, flags=flags)
    assert k == n, (f, pat, k)
    write(f, s2)
