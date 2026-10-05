"""List every file deleted since BASE that something in the tree still names.

check_lists.py checks the source lists; this also reads resource lists
(nice_target_sources with ${res_loc}), .qrc files, .style imports and
#includes, by searching for each deleted file's path below Resources/ or
SourceFiles/, and for resources also its bare file name.

Usage, from the repo root: python loogrigram-tools/check_deleted_refs.py BASE
(BASE: the last commit that built, e.g. the last release's).
"""
import os
import re
import subprocess
import sys

base = sys.argv[1] if len(sys.argv) > 1 else 'HEAD~1'
deleted = subprocess.run(
    ['git', 'diff', '--name-only', '--diff-filter=D', base + '..HEAD'],
    capture_output=True, text=True, check=True).stdout.split()
tracked = subprocess.run(
    ['git', 'ls-files', 'Telegram'],
    capture_output=True, text=True, check=True).stdout.split()
scan = [f for f in tracked
        if f.endswith(('.cmake', 'CMakeLists.txt', '.qrc', '.style', '.cpp',
                       '.h', '.mm', '.rc', '.txt', '.py'))
        and '/ThirdParty/' not in f]
texts = {}
for f in scan:
    try:
        texts[f] = open(f, encoding='utf-8', errors='replace').read()
    except OSError:
        pass

RESOURCE = ('.qrc', '.style', '.css', '.js', '.html', '.tgs', '.png', '.svg',
            '.webp', '.lottie', '.json')
found = 0
for path in deleted:
    keys = {path.split(anchor, 1)[1]
            for anchor in ('Resources/', 'SourceFiles/') if anchor in path}
    name = path.split('/')[-1]
    for f, t in texts.items():
        hit = [k for k in keys if k in t]
        if not hit and name.endswith(RESOURCE) and re.search(
                r'[/"\s>]' + re.escape(name) + r'\b', t):
            hit = [name]
        if hit:
            found += 1
            line = next(i for i, l in enumerate(t.split('\n'), 1)
                        if any(h in l for h in hit))
            print('%s  <- named in %s:%d' % (path, f, line))
print('%d deleted since %s, %d stale reference(s)' % (
    len(deleted), base, found))
sys.exit(1 if found else 0)
