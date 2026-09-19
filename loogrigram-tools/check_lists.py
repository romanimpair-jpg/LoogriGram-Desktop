"""The two cheap checks LOOGRIGRAM.md asks for after any deletion.

1. Every source path in CMakeLists.txt, cmake/td_*.cmake and every .qrc
   exists. Entries inside remove_target_sources(...) are skipped: they name
   files that are allowed not to exist.
2. Every icon a .style names resolves to a file under Resources/icons or a
   lib_*/**/icons directory (`-WxH` and `-flip_*` are codegen directives, not
   part of the name). Comments are stripped first.

Run: python3 check_lists.py [path to the Telegram directory]
Known and harmless: the Updater target's two base_windows_safe_library lines
are relative to lib_base, a submodule not checked out locally, and the
Updater is not built (DESKTOP_APP_DISABLE_AUTOUPDATE). Anything else is real.
"""
import glob
import os
import re
import sys

T = sys.argv[1] if len(sys.argv) > 1 else 'Telegram'
S = os.path.join(T, 'SourceFiles')
bad = 0

EXT = r'\.(?:cpp|h|mm|c|style|palette|rc|manifest|def)'
lists = [os.path.join(T, 'CMakeLists.txt')] + \
    glob.glob(os.path.join(T, 'cmake', 'td_*.cmake'))
for f in lists:
    removing = False
    for n, line in enumerate(open(f, encoding='utf-8'), 1):
        if 'remove_target_sources(' in line:
            removing = True
        if removing:
            if ')' in line:
                removing = False
            continue
        m = re.match(r'^\s+([A-Za-z0-9_./-]+' + EXT + r')\s*$', line)
        if not m:
            continue
        rel = m.group(1)
        cands = [os.path.join(S, rel), os.path.join(T, 'Resources', rel)]
        if not any(os.path.exists(c) for c in cands):
            print(f'{os.path.basename(f)}:{n}: missing {rel}')
            bad += 1

for f in glob.glob(os.path.join(T, 'Resources', 'qrc', '**', '*.qrc'), recursive=True):
    base = os.path.dirname(f)
    for n, line in enumerate(open(f, encoding='utf-8'), 1):
        m = re.search(r'<file[^>]*>([^<]+)</file>', line)
        if m and not os.path.exists(os.path.join(base, m.group(1))):
            print(f'{os.path.relpath(f, T)}:{n}: missing {m.group(1)}')
            bad += 1

icon_dirs = [os.path.join(T, 'Resources', 'icons')] + \
    glob.glob(os.path.join(T, 'lib_*', '**', 'icons'), recursive=True)
styles = glob.glob(os.path.join(S, '**', '*.style'), recursive=True) + \
    glob.glob(os.path.join(T, 'lib_*', '**', '*.style'), recursive=True)
for f in styles:
    text = open(f, encoding='utf-8').read()
    text = re.sub(r'//[^\n]*', lambda m: ' ' * len(m.group(0)), text)
    text = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text, flags=re.S)
    for m in re.finditer(r'\{\s*"([A-Za-z0-9_/.-]+)"\s*,', text):
        name = m.group(1).lstrip('/')
        stem = re.sub(r'-(?:\d+x\d+|flip_[a-z]+)', '', name)
        found = any(
            os.path.exists(os.path.join(d, stem + ext))
            for d in icon_dirs for ext in ('.png', '.svg', '@2x.png', '@3x.png'))
        if not found:
            line = text.count('\n', 0, m.start()) + 1
            print(f'{os.path.relpath(f, T)}:{line}: icon not found "{name}"')
            bad += 1

print('bad:', bad)
