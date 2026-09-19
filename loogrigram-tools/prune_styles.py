"""Remove every unreachable style entry from every .style file, repeatedly,
until nothing more becomes unreachable.

Reachability is the same as unused_styles.py: roots are st::Name uses in
C++ sources; references between style definitions are followed across
files. Entries whose definition line says [[maybe_unused]] are upstream's
deliberate constants and are kept. Each removed chunk and each resulting
file must be brace-balanced.

Run from the desktop checkout. Prints what it removed per file.
"""
import glob
import os
import re


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


def norm(path):
    return path.replace(os.sep, '/')


def load():
    style_files = [norm(p) for p in
                   glob.glob(S + '**/*.style', recursive=True)
                   + glob.glob(T + 'lib_ui/**/*.style', recursive=True)]
    defs, body, line = {}, {}, {}
    for path in style_files:
        text = open(path, encoding='utf-8').read()
        matches = list(re.finditer(r'(?m)^([a-zA-Z][A-Za-z0-9_]*):', mask_comments(text)))
        for i, m in enumerate(matches):
            end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
            defs[m.group(1)] = path
            body[m.group(1)] = text[m.end():end]
            eol = text.find('\n', m.start())
            line[m.group(1)] = text[m.start():eol]
    return style_files, defs, body, line


code_roots = set()
for path in (glob.glob(S + '**/*.cpp', recursive=True)
             + glob.glob(S + '**/*.h', recursive=True)):
    text = open(norm(path), encoding='utf-8', errors='ignore').read()
    for m in re.finditer(r'st::([A-Za-z0-9_]+)', text):
        code_roots.add(m.group(1))

total = 0
for round_ in range(20):
    style_files, defs, body, line = load()
    seen, stack = set(), [r for r in code_roots if r in defs]
    stack += [n for n in defs if 'maybe_unused' in line[n]]
    # lib_ui styles are not ours to prune; treat them as roots.
    stack += [n for n, p in defs.items() if p.startswith(T + 'lib_ui/')]
    while stack:
        name = stack.pop()
        if name in seen:
            continue
        seen.add(name)
        for m in re.finditer(r'\b([a-zA-Z][A-Za-z0-9_]*)\b', body.get(name, '')):
            if m.group(1) in defs and m.group(1) not in seen:
                stack.append(m.group(1))
    dead = {n for n in defs if n not in seen}
    if not dead:
        break
    by_file = {}
    for n in dead:
        by_file.setdefault(defs[n], set()).add(n)
    for path, names in sorted(by_file.items()):
        text = open(path, encoding='utf-8', newline='').read()
        matches = list(re.finditer(r'(?m)^([a-zA-Z][A-Za-z0-9_]*):', mask_comments(text)))
        out, cursor = [], 0
        for i, m in enumerate(matches):
            if m.group(1) not in names:
                continue
            end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
            chunk = text[m.start():end]
            assert chunk.count('{') == chunk.count('}'), (path, m.group(1))
            out.append(text[cursor:m.start()])
            cursor = end
        out.append(text[cursor:])
        result = re.sub(r'\n{3,}', '\n\n', ''.join(out))
        assert result.count('{') == result.count('}'), path
        open(path, 'w', encoding='utf-8', newline='').write(result)
        print(f'round {round_ + 1}: {path}: -{len(names)}')
        total += len(names)
print('removed', total)
