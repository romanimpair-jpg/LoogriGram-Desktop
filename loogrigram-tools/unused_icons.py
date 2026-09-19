"""Report icon files under Resources/icons that nothing names.

A .style names an icon without extension and may add codegen suffixes
(-WxH, -flip_*). Sources and .qrc files can also name one by path. Retina
variants (@2x, @3x) belong to their base name. A .qrc names one by its alias, icons/...;
an icon loaded by a computed name cannot be seen, so check .tgs/.lottie by hand.
"""
import glob
import os
import re

T = 'Telegram/'
S = T + 'SourceFiles/'
ICONS = T + 'Resources/icons/'


def norm(path):
    return path.replace(os.sep, '/')


names = set()
for pattern, files in (
        ('style', glob.glob(S + '**/*.style', recursive=True)
            + glob.glob(T + 'lib_ui/**/*.style', recursive=True)),
        ('code', glob.glob(S + '**/*.cpp', recursive=True)
            + glob.glob(S + '**/*.h', recursive=True)
            + glob.glob(T + 'Resources/qrc/**/*.qrc', recursive=True))):
    for path in files:
        text = open(norm(path), encoding='utf-8', errors='ignore').read()
        for m in re.finditer(r'"([A-Za-z0-9_/.-]+)"', text):
            raw = m.group(1).lstrip('/')
            raw = re.sub(r'^:?/?gui/icons/', '', raw)
            raw = re.sub(r'^(?:\.\./)*icons/', '', raw)
            raw = re.sub(r'\.(png|svg|tgs|lottie)$', '', raw)
            raw = re.sub(r'-(?:\d+x\d+|flip_[a-z]+)', '', raw)
            names.add(raw)

unused = []
for path in glob.glob(ICONS + '**/*.*', recursive=True):
    path = norm(path)
    rel = path[len(ICONS):]
    stem = re.sub(r'\.(png|svg|tgs|lottie)$', '', rel)
    stem = re.sub(r'@[23]x$', '', stem)
    if stem not in names:
        unused.append(rel)
print(len(unused), 'unreferenced icon files')
for rel in sorted(unused):
    print(rel)
