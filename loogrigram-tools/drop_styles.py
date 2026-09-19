"""Remove named top-level definitions from a .style file.

Each cut runs from the definition's own line to the start of the next
top-level definition, and must be brace-balanced; the whole file is checked
for balance afterwards too, because nothing else catches an unbalanced
.style until the codegen runs.
"""
import re
import sys


def mask_comments(text):
    """Blank out comments, keeping offsets, so a URL in the licence header
    (https://...) is never read as a definition named "https"."""
    text = re.sub(
        r'/\*.*?\*/',
        lambda m: re.sub(r'[^\n]', ' ', m.group(0)),
        text,
        flags=re.S)
    return re.sub(r'//[^\n]*', lambda m: ' ' * len(m.group(0)), text)

path = sys.argv[1]
names = set(name.strip() for name in sys.argv[2:] if name.strip())
text = open(path, encoding='utf-8', newline='').read()
# A struct type block ("Name {", no colon) is a boundary too: without it the
# cut before a type definition ran on and swallowed the whole type.
matches = list(re.finditer(
    r'(?m)^([a-zA-Z][A-Za-z0-9_]*)(?::| \{)',
    mask_comments(text)))
spans = []
for i, m in enumerate(matches):
    end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
    if m.group(1) in names:
        spans.append((m.start(), end, m.group(1)))
found = {s[2] for s in spans}
missing = names - found
assert not missing, missing
result, cursor, removed = [], 0, 0
for start, end, name in spans:
    chunk = text[start:end]
    assert chunk.count('{') == chunk.count('}'), (name, chunk)
    result.append(text[cursor:start])
    cursor = end
    removed += 1
result.append(text[cursor:])
out = ''.join(result)
assert out.count('{') == out.count('}'), 'file unbalanced'
out = re.sub(r'\n{3,}', '\n\n', out)
open(path, 'w', encoding='utf-8', newline='').write(out)
print('removed', removed, 'of', len(names))
