"""Report style entries in a .style file that nothing can reach any more.

Roots are every st::Name in the C++ sources plus every name referenced from
any other .style file; from those we follow references between style
definitions. Anything in the target file not reached that way is dead.
"""
import glob
import os
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

T = 'Telegram/'
S = T + 'SourceFiles/'
target = sys.argv[1] if len(sys.argv) > 1 else S + 'iv/iv.style'


def norm(path):
    return path.replace(os.sep, '/')


style_files = [norm(p) for p in
               glob.glob(S + '**/*.style', recursive=True)
               + glob.glob(T + 'lib_ui/**/*.style', recursive=True)]
code_files = [norm(p) for p in
              glob.glob(S + '**/*.cpp', recursive=True)
              + glob.glob(S + '**/*.h', recursive=True)]

defs, body = {}, {}
for path in style_files:
    text = open(path, encoding='utf-8').read()
    matches = list(re.finditer(r'(?m)^([a-zA-Z][A-Za-z0-9_]*):', mask_comments(text)))
    for i, m in enumerate(matches):
        end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
        defs[m.group(1)] = path
        body[m.group(1)] = text[m.end():end]

roots = set()
for path in code_files:
    text = open(path, encoding='utf-8', errors='ignore').read()
    for m in re.finditer(r'st::([A-Za-z0-9_]+)', text):
        roots.add(m.group(1))
for name, path in defs.items():
    if path != target:
        for m in re.finditer(r'\b([a-zA-Z][A-Za-z0-9_]*)\b', body[name]):
            roots.add(m.group(1))

seen, stack = set(), [r for r in roots if r in defs]
while stack:
    name = stack.pop()
    if name in seen:
        continue
    seen.add(name)
    for m in re.finditer(r'\b([a-zA-Z][A-Za-z0-9_]*)\b', body.get(name, '')):
        if m.group(1) in defs and m.group(1) not in seen:
            stack.append(m.group(1))

mine = [n for n, p in defs.items() if p == target]
unused = sorted(n for n in mine if n not in seen)
print(len(mine), 'defined in', target, '-', len(unused), 'unreachable')
print('\n'.join(unused))
