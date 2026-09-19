"""Find what the style code generator would reject after a deletion.

Reports, across every .style file (ours and lib_*), plus the palettes:
  - struct types used by a definition, or as a field type inside another
    struct type, but defined nowhere;
  - parents in Type(parent, ...) that are defined nowhere;
  - fields an instance sets that its struct type does not have;
  - names used inside values (name: other; field: margins(a, 0px, b, 0px);
    icon colours...) that nothing defines anywhere.
codegen_style stops at the first error, so one CI failure can hide many.
Run from the repository root: python3 tools/check_styles.py
"""
import glob
import os
import re
import sys

BUILTIN_TYPES = {
    'int', 'bool', 'double', 'pixels', 'string', 'color', 'icon', 'font',
    'margins', 'point', 'size', 'align', 'cursor', 'rect',
}
# Words that may stand alone as values without naming a definition.
BUILTIN_VALUES = {
    'true', 'false', 'transparent', 'cursor', 'default', 'pointer', 'text',
    'center', 'left', 'right', 'top', 'bottom', 'topleft', 'topright',
    'bottomleft', 'bottomright', 'topcenter', 'bottomcenter', 'centerleft',
    'centerright', 'none',
    # functions and keywords inside compound values
    'margins', 'point', 'size', 'font', 'icon', 'rgb', 'rgba', 'align',
    'bold', 'italic', 'semibold', 'underline', 'monospace', 'px', 'flip',
    'rect', 'textalign',
}

IDENT = r'[A-Za-z_][A-Za-z0-9_]*'
# name: Type {  or  name: Type(parent, parent2) {
INSTANCE = re.compile(
    r'\s*(' + IDENT + r')\s*(\(\s*' + IDENT
    + r'(?:\s*,\s*' + IDENT + r')*\s*\))?\s*\{')
FIELD = re.compile(r'\s*(' + IDENT + r')\s*:\s*')
STRUCT_FIELD = re.compile(r'(' + IDENT + r')\s*:\s*(' + IDENT + r')\s*;')


class Model:
    def __init__(self):
        self.types = {}      # struct type -> (path, line)
        self.fields = {}     # struct type -> set of field names
        self.variables = {}  # defined name -> (path, line)
        self.uses = []       # (kind, name, path, line)
        self.sets = []       # (struct type, field, path, line)


def value_names(value):
    """Identifiers inside a compound value that must name a definition."""
    names = []
    for m in re.finditer(r'(?<![A-Za-z0-9_#.])(' + IDENT + r')', value):
        if m.group(1) not in BUILTIN_VALUES:
            names.append(m.group(1))
    return names


def mask_comments(text):
    text = re.sub(
        r'/\*.*?\*/',
        lambda m: re.sub(r'[^\n]', ' ', m.group(0)),
        text,
        flags=re.S)
    return re.sub(r'//[^\n]*', lambda m: ' ' * len(m.group(0)), text)


def mask_strings(text):
    return re.sub(
        r'"[^"\n]*"',
        lambda m: '"' + ' ' * (len(m.group(0)) - 2) + '"',
        text)


def line_of(text, pos):
    return text.count('\n', 0, pos) + 1


def match_brace(text, i):
    depth = 0
    for k in range(i, len(text)):
        if text[k] == '{':
            depth += 1
        elif text[k] == '}':
            depth -= 1
            if depth == 0:
                return k
    return len(text) - 1


def parents(group):
    return re.findall(IDENT, group) if group else []


def value_end(text, k, stop_at_close):
    """End of a plain value starting at k: the ';' at depth 0."""
    depth = 0
    j = k
    while j < len(text):
        c = text[j]
        if c in '{(':
            depth += 1
        elif c in '})':
            if depth == 0 and stop_at_close:
                break
            depth -= 1
        elif c == ';' and depth == 0:
            break
        j += 1
    return j


def parse(model, path, text):
    """Walk top-level statements."""
    i = 0
    n = len(text)
    while i < n:
        i = re.compile(r'\s*').match(text, i).end()
        if i >= n:
            break
        if text.startswith('using', i):
            i = text.index(';', i) + 1
            continue
        m = re.compile(IDENT).match(text, i)
        if not m:
            i += 1
            continue
        name = m.group(0)
        k = re.compile(r'\s*').match(text, m.end()).end()
        if k < n and text[k] == '{':
            # A struct type: Name { field: type; ... }
            end = match_brace(text, k)
            model.types[name] = (path, line_of(text, i))
            model.fields[name] = set()
            body = text[k + 1:end]
            for f in STRUCT_FIELD.finditer(body):
                model.fields[name].add(f.group(1))
                model.uses.append(
                    ('type', f.group(2), path, line_of(text, k + 1 + f.start())))
            i = end + 1
            continue
        if k < n and text[k] == ':':
            k += 1
            model.variables[name] = (path, line_of(text, i))
            m3 = INSTANCE.match(text, k)
            if m3 and m3.group(1) != 'icon':
                tname = m3.group(1)
                model.uses.append(('type', tname, path, line_of(text, i)))
                for parent in parents(m3.group(2)):
                    model.uses.append(('value', parent, path, line_of(text, i)))
                brace = text.index('{', m3.start(1))
                end = match_brace(text, brace)
                collect_fields(
                    model,
                    text[brace + 1:end],
                    path,
                    line_of(text, brace),
                    tname)
                i = end + 1
                continue
            j = value_end(text, k, False)
            for word in value_names(text[k:j]):
                model.uses.append(('value', word, path, line_of(text, i)))
            i = j + 1
            continue
        i = k + 1


def collect_fields(model, body, path, base_line, tname):
    """field: value; pairs inside an instance body, nested instances too."""
    i = 0
    n = len(body)
    while i < n:
        m = FIELD.match(body, i)
        if not m:
            i += 1
            continue
        line = base_line + body.count('\n', 0, m.start(1))
        model.sets.append((tname, m.group(1), path, line))
        k = m.end()
        m3 = INSTANCE.match(body, k)
        if m3 and m3.group(1) != 'icon':
            model.uses.append(('type', m3.group(1), path, line))
            for parent in parents(m3.group(2)):
                model.uses.append(('value', parent, path, line))
            brace = body.index('{', m3.start(1))
            end = match_brace(body, brace)
            collect_fields(
                model,
                body[brace + 1:end],
                path,
                base_line + body.count('\n', 0, brace),
                m3.group(1))
            i = end + 1
            continue
        j = value_end(body, k, True)
        for word in value_names(body[k:j]):
            model.uses.append(('value', word, path, line))
        i = j + 1


def main():
    norm = lambda p: p.replace(os.sep, '/')
    files = [norm(p) for p in glob.glob('Telegram/**/*.style', recursive=True)]
    palettes = [norm(p) for p in glob.glob('Telegram/**/*.palette', recursive=True)]
    model = Model()
    for path in palettes:
        text = mask_comments(open(path, encoding='utf-8').read())
        for m in re.finditer(r'^\s*(' + IDENT + r')\s*:', text, re.M):
            model.variables[m.group(1)] = (path, line_of(text, m.start()))
    for path in files:
        text = mask_strings(mask_comments(open(path, encoding='utf-8').read()))
        parse(model, path, text)
    bad = 0
    for kind, name, path, line in model.uses:
        if kind == 'type':
            if name not in model.types and name not in BUILTIN_TYPES:
                print('%s:%d: undefined struct type %s' % (path, line, name))
                bad += 1
        elif name not in model.variables and name not in model.types:
            print('%s:%d: undefined name %s' % (path, line, name))
            bad += 1
    for tname, field, path, line in model.sets:
        if tname in model.fields and field not in model.fields[tname]:
            print('%s:%d: %s has no field %s' % (path, line, tname, field))
            bad += 1
    print('%d style files, %d types, %d names, %d problems' % (
        len(files), len(model.types), len(model.variables), bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
