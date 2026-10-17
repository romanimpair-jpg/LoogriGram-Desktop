#!/usr/bin/env python3
# LoogriGram: the Windows build's own cache, which replaces GitHub's.
"""The build store: the Windows build's cache, kept as the assets of one
pre-release of this repository (tag build-store). GitHub's Actions caches
are evicted after seven days unused, so a build after a quiet month rebuilt
every library; nothing here expires, so a build months from now starts from
whatever the last one left.

An entry is a set of paths under the build root, stored as a zstd tar split
into parts under GitHub's 2 GiB asset limit and described in manifest.json:
each part's size and SHA-256, the commit and run that made it, a fingerprint
of its files, and the machine it was built on. Two versions of each entry
are kept; the older is the fallback when the newer cannot be read.

  restore ENTRY...  the newest usable version of each entry, into the root
  save ENTRY...     upload each entry whose files changed since its newest
                    version (every time, with --always)
  status            what the store holds
  drop ENTRY...     forget entries and delete their parts
  put FILE...       upload files as they are, unless already present
                    (the pinned tools' mirror)

Entries: thirdparty; libraries-CONFIG and qt-CONFIG, the libraries without
and with Qt, split as the Actions caches were; out-CONFIG, the app's build
tree; selftest, for trying the store out. Which library gets rebuilt stays
prepare.py's decision, made per stage from its keys; the store only keeps
what was built, so a dependency bump still rebuilds just what depends on it.

A restore compares the machine that built the version with this one and
reports each difference as a warning (compiler, SDK, build tools) or a
notice (image, git, gh, tar, zstd), then goes on: a difference is where to
look first if the build fails, not a reason to stop it.
"""

import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

FORMAT = 1
MANIFEST = 'manifest.json'
KEEP = 2
PART_SIZE = 1900 * 1024 * 1024
CHUNK = 8 * 1024 * 1024
QT = re.compile(r'^[qQ]t[_-]')
PART_NAME = re.compile(r'^(?P<entry>.+)-(?P<id>\d{8}T\d{6}Z-[^.]+)\.tar\.zst\.\d{2}$')
WARN_KEYS = ('VCToolsVersion', 'WindowsSDKVersion', 'cmake', 'ninja', 'perl', 'python')
NOTES = ('The Windows build\'s own cache: built libraries, the app\'s build '
         'tree and mirrored build tools, restored and updated by every build '
         '(.github/scripts/build_store.py). Not an app release - the in-app '
         'updater reads only the latest release, which this never is.')


def log(text):
    print(text, flush=True)


def annotate(level, title, text):
    log('::%s title=%s::%s' % (level, title, text))


def posix(*parts):
    return '/'.join(p.strip('/\\').replace('\\', '/') for p in parts if p)


def safe_rel(rel):
    rel = rel.replace('\\', '/')
    if not rel or rel.startswith('/') or re.match(r'^[A-Za-z]:', rel) \
            or '..' in rel.split('/'):
        raise SystemExit('refusing path %r' % rel)
    return rel


def human(size):
    for unit in ('B', 'KB', 'MB', 'GB'):
        if size < 1024 or unit == 'GB':
            return '%.1f %s' % (size, unit) if unit != 'B' else '%d B' % size
        size /= 1024.0


def retry(what, action, attempts=3):
    for attempt in range(1, attempts + 1):
        try:
            return action()
        except Exception as error:  # noqa: BLE001 - every failure is retried
            if attempt == attempts:
                raise
            log('%s failed (%s), retrying' % (what, error))
            time.sleep(10 * attempt)


# Where the store lives: a release of the repository, or a directory (tests).

class GhStore:
    def __init__(self, repo, tag):
        self.repo = repo
        self.tag = tag

    def _gh(self, args, capture=True):
        result = subprocess.run(
            ['gh'] + args + ['--repo', self.repo],
            capture_output=capture, text=True)
        if result.returncode != 0:
            raise RuntimeError('gh %s: %s' % (
                ' '.join(args[:2]),
                (result.stderr or result.stdout or '').strip()))
        return result.stdout

    def assets(self):
        try:
            out = self._gh(['release', 'view', self.tag, '--json', 'assets'])
        except RuntimeError as error:
            if 'not found' in str(error).lower():
                return {}
            raise
        return {a['name']: a['size'] for a in json.loads(out)['assets']}

    def ensure(self):
        try:
            self._gh(['release', 'view', self.tag, '--json', 'tagName'])
        except RuntimeError as error:
            if 'not found' not in str(error).lower():
                raise
            self._gh(['release', 'create', self.tag, '--prerelease',
                      '--latest=false', '--title', 'Build store',
                      '--notes', NOTES])
            log('created release %s' % self.tag)

    def download(self, name, dest):
        directory = os.path.dirname(dest)
        retry('downloading ' + name, lambda: self._gh(
            ['release', 'download', self.tag, '--pattern', name,
             '--dir', directory, '--clobber']))
        if not os.path.isfile(dest):
            raise RuntimeError('%s did not download' % name)

    def upload(self, paths, clobber=False):
        args = ['release', 'upload', self.tag] + list(paths)
        if clobber:
            args.append('--clobber')
        retry('uploading ' + ', '.join(map(os.path.basename, paths)),
              lambda: self._gh(args))

    def delete(self, name):
        retry('deleting ' + name, lambda: self._gh(
            ['release', 'delete-asset', self.tag, name, '--yes']))


class DirStore:
    def __init__(self, path):
        self.path = path

    def assets(self):
        if not os.path.isdir(self.path):
            return {}
        return {n: os.path.getsize(os.path.join(self.path, n))
                for n in os.listdir(self.path)}

    def ensure(self):
        os.makedirs(self.path, exist_ok=True)

    def download(self, name, dest):
        source = os.path.join(self.path, name)
        if not os.path.isfile(source):
            raise RuntimeError('%s is not in the store' % name)
        shutil.copyfile(source, dest)

    def upload(self, paths, clobber=False):
        for path in paths:
            target = os.path.join(self.path, os.path.basename(path))
            if os.path.exists(target) and not clobber:
                raise RuntimeError('%s exists' % os.path.basename(path))
            shutil.copyfile(path, target)

    def delete(self, name):
        os.remove(os.path.join(self.path, name))


def open_store(spec):
    kind, _, rest = spec.partition(':')
    if kind == 'dir' and rest:
        return DirStore(rest)
    if kind == 'gh' and rest.count(':') == 1:
        repo, tag = rest.split(':')
        return GhStore(repo, tag)
    raise SystemExit('--store is gh:OWNER/REPO:TAG or dir:PATH, not %r' % spec)


def load_manifest(store, scratch):
    if MANIFEST not in store.assets():
        return {'format': FORMAT, 'entries': {}}
    path = os.path.join(scratch, MANIFEST)
    store.download(MANIFEST, path)
    with open(path, encoding='utf-8') as f:
        manifest = json.load(f)
    os.remove(path)
    if manifest.get('format') != FORMAT:
        raise SystemExit('manifest format %r, this script reads %d'
                         % (manifest.get('format'), FORMAT))
    manifest.setdefault('entries', {})
    return manifest


def write_manifest(store, manifest, scratch):
    path = os.path.join(scratch, MANIFEST)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
        f.write('\n')
    store.upload([path], clobber=True)
    os.remove(path)


# What an entry is, and what it looks like on disk.

def entry_paths(name, root, libraries, tree):
    def existing(rels):
        return [r for r in rels if os.path.lexists(os.path.join(root, r))]

    if name == 'thirdparty':
        return existing(['ThirdParty'])
    if name == 'selftest':
        return existing(['selftest'])
    kind, _, config = name.partition('-')
    if kind == 'out' and config:
        return existing([tree])
    if kind in ('libraries', 'qt') and config:
        want_qt = kind == 'qt'
        base = os.path.join(root, libraries)
        paths = []
        if os.path.isdir(base):
            for child in sorted(os.listdir(base)):
                if child != 'cache_keys' and bool(QT.match(child)) == want_qt:
                    paths.append(posix(libraries, child))
            keys = os.path.join(base, 'cache_keys')
            if os.path.isdir(keys):
                for child in sorted(os.listdir(keys)):
                    if bool(QT.match(child)) == want_qt:
                        paths.append(posix(libraries, 'cache_keys', child))
        return paths
    raise SystemExit('unknown entry %r' % name)


def walk_files(root, rel):
    full = os.path.join(root, rel)
    if not os.path.isdir(full) or os.path.islink(full):
        yield full, rel
        return
    for dirpath, dirnames, filenames in os.walk(full):
        dirnames.sort()
        for name in sorted(filenames):
            path = os.path.join(dirpath, name)
            yield path, posix(rel, os.path.relpath(path, full))


def fingerprint(root, paths):
    """Files, sizes and whole-second mtimes - what a restore reproduces
    exactly, since tar keeps mtimes to the second. A prepare.py key file is
    read by content instead: a stage rebuilt to the same key rewrites its
    key file with nothing new in it (gas-preprocessor, whose only file the
    clean-up step deletes, is rebuilt that way on every run)."""
    digest = hashlib.sha256()
    files = total = 0
    for rel in paths:
        for path, relpath in walk_files(root, rel):
            st = os.lstat(path)
            if '/cache_keys/' in '/' + relpath:
                with open(path, 'rb') as f:
                    tag = hashlib.sha256(f.read()).hexdigest()
            else:
                tag = '%d:%d' % (st.st_size, int(st.st_mtime))
            digest.update(('%s\0%s\n' % (relpath, tag)).encode(
                'utf-8', 'surrogateescape'))
            files += 1
            total += st.st_size
    return digest.hexdigest(), files, total


def _writable_then_retry(function, path, _info):
    # Windows refuses to delete a read-only file; git leaves its objects so.
    os.chmod(path, 0o666)
    function(path)


def remove_paths(root, rels):
    for rel in rels:
        path = os.path.join(root, safe_rel(rel))
        if os.path.islink(path) or os.path.isfile(path):
            try:
                os.remove(path)
            except PermissionError:
                _writable_then_retry(os.remove, path, None)
        elif os.path.isdir(path):
            if sys.version_info >= (3, 12):
                shutil.rmtree(path, onexc=_writable_then_retry)
            else:
                shutil.rmtree(path, onerror=_writable_then_retry)


# The machine an entry was built on.

_MACHINE = None


def first_line(cmd):
    try:
        result = subprocess.run(cmd, capture_output=True, text=True,
                                timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return None
    text = (result.stdout or result.stderr or '').strip()
    return text.splitlines()[0].strip() if text else None


def machine(zstd):
    global _MACHINE
    if _MACHINE is None:
        found = {}
        for key in ('ImageOS', 'ImageVersion', 'VCToolsVersion',
                    'WindowsSDKVersion'):
            value = os.environ.get(key, '').strip().rstrip('\\')
            if value:
                found[key] = value
        for key, cmd in (('cmake', ['cmake', '--version']),
                         ('ninja', ['ninja', '--version']),
                         ('perl', ['perl', '-e', 'print $^V']),
                         ('git', ['git', '--version']),
                         ('gh', ['gh', '--version']),
                         ('tar', ['tar', '--version']),
                         ('zstd', [zstd, '-V'])):
            value = first_line(cmd)
            if value:
                found[key] = value
        found['python'] = sys.version.split()[0]
        _MACHINE = found
    return _MACHINE


def compare_machine(name, stored, zstd):
    now = machine(zstd)
    differences = 0
    for key in sorted(set(stored) | set(now)):
        before, after = stored.get(key), now.get(key)
        if before == after:
            continue
        differences += 1
        annotate('warning' if key in WARN_KEYS else 'notice',
                 'Build store: ' + name,
                 '%s was built with %s %s; this runner has %s' % (
                     name, key, before or '(not recorded)',
                     after or '(none)'))
    if not differences:
        log('%s: built on a machine like this one' % name)


# Archives.

def find_zstd(given):
    for candidate in (given, os.environ.get('ZSTD'), shutil.which('zstd')):
        if candidate and os.path.isfile(candidate):
            return candidate
    raise SystemExit('zstd not found: pass --zstd or put it on PATH')


def pack(root, paths, directory, prefix, part_size, zstd):
    listing = os.path.join(directory, prefix + '.list')
    with open(listing, 'wb') as f:
        for rel in paths:
            f.write(rel.encode('utf-8') + b'\0')
    tar = subprocess.Popen(
        ['tar', '-cf', '-', '--null', '-T', listing.replace('\\', '/')],
        cwd=root, stdout=subprocess.PIPE)
    zst = subprocess.Popen([zstd, '-q', '-T0', '-3', '-c'],
                           stdin=tar.stdout, stdout=subprocess.PIPE)
    tar.stdout.close()
    parts = []
    out = digest = None
    written = 0
    while True:
        chunk = zst.stdout.read(CHUNK)
        if not chunk:
            break
        view = memoryview(chunk)
        while view:
            if out is None:
                name = '%s.tar.zst.%02d' % (prefix, len(parts))
                out = open(os.path.join(directory, name), 'wb')
                digest = hashlib.sha256()
                written = 0
            take = min(len(view), part_size - written)
            out.write(view[:take])
            digest.update(view[:take])
            written += take
            view = view[take:]
            if written == part_size:
                out.close()
                parts.append({'name': name, 'size': written,
                              'sha256': digest.hexdigest()})
                out = None
    if out is not None:
        out.close()
        parts.append({'name': name, 'size': written,
                      'sha256': digest.hexdigest()})
    zst_code, tar_code = zst.wait(), tar.wait()
    os.remove(listing)
    if tar_code or zst_code:
        raise RuntimeError('tar exited %d, zstd %d' % (tar_code, zst_code))
    if not parts:
        raise RuntimeError('the archive came out empty')
    return parts


def unpack(root, files, zstd):
    zst = subprocess.Popen([zstd, '-q', '-d', '-c'],
                           stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    tar = subprocess.Popen(['tar', '-xf', '-'], cwd=root, stdin=zst.stdout)
    zst.stdout.close()
    try:
        for path in files:
            with open(path, 'rb') as f:
                while True:
                    chunk = f.read(CHUNK)
                    if not chunk:
                        break
                    zst.stdin.write(chunk)
    except BrokenPipeError:
        pass
    finally:
        zst.stdin.close()
    zst_code, tar_code = zst.wait(), tar.wait()
    if tar_code or zst_code:
        raise RuntimeError('zstd exited %d, tar %d' % (zst_code, tar_code))


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as f:
        while True:
            chunk = f.read(CHUNK)
            if not chunk:
                return digest.hexdigest()
            digest.update(chunk)


# State shared by the steps of one job: which version of each entry this
# run restored or saved, so the build tree knows what it was built against.

def read_state(path):
    if path and os.path.isfile(path):
        with open(path, encoding='utf-8') as f:
            return json.load(f)
    return {}


def write_state(path, state):
    if path:
        with open(path, 'w', encoding='utf-8') as f:
            json.dump(state, f, indent=1, sort_keys=True)


def new_id():
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime(
        '%Y%m%dT%H%M%SZ')
    run = os.environ.get('GITHUB_RUN_ID', 'local')
    attempt = os.environ.get('GITHUB_RUN_ATTEMPT', '1')
    return '%s-%s-%s' % (stamp, run, attempt) if run != 'local' \
        else '%s-local' % stamp


def run_url():
    server = os.environ.get('GITHUB_SERVER_URL')
    repo = os.environ.get('GITHUB_REPOSITORY')
    run = os.environ.get('GITHUB_RUN_ID')
    return '%s/%s/actions/runs/%s' % (server, repo, run) \
        if server and repo and run else None


# Commands.

def salted(name):
    # OUT_CACHE_SALT is the build tree's: raising it discards every tree.
    return name.startswith('out-')


def unusable(name, version, args, state):
    if salted(name) and args.salt is not None \
            and version.get('salt') != args.salt:
        return 'salt %r, this run uses %r' % (version.get('salt'), args.salt)
    for dep, built in sorted((version.get('built_against') or {}).items()):
        have = state.get(dep)
        if built and have and have < built:
            return ('built against %s %s, newer than the %s this run has'
                    % (dep, built, have))
    return None


def restore_one(store, manifest, name, args, state, scratch, zstd):
    """True unless a version failed to restore for want of something that
    should have worked - a download, a checksum, the unpacking. Being absent
    from the store, or refused by a rule, is not a failure."""
    versions = manifest['entries'].get(name, {}).get('versions', [])
    if not versions:
        log('%s: not in the store' % name)
        return True
    failed = False
    for version in versions:
        why = unusable(name, version, args, state)
        if why:
            log('%s %s: skipped, %s' % (name, version['id'], why))
            continue
        compare_machine(name, version.get('machine', {}), zstd)
        directory = tempfile.mkdtemp(prefix='restore-', dir=scratch)
        started = time.time()
        try:
            files = []
            for part in version['parts']:
                path = os.path.join(directory, part['name'])
                store.download(part['name'], path)
                if os.path.getsize(path) != part['size'] \
                        or sha256_file(path) != part['sha256']:
                    raise RuntimeError('%s does not match its checksum'
                                       % part['name'])
                files.append(path)
            fetched = time.time()
            remove_paths(args.root, version.get('paths', []))
            unpack(args.root, files, zstd)
        except Exception as error:  # noqa: BLE001 - fall back to the next
            annotate('warning', 'Build store: ' + name,
                     '%s %s could not be restored: %s'
                     % (name, version['id'], error))
            remove_paths(args.root, version.get('paths', []))
            failed = True
            continue
        finally:
            shutil.rmtree(directory, ignore_errors=True)
        state[name] = version['id']
        log('%s: restored %s (%s, %d files, commit %s) - %.0fs download, '
            '%.0fs unpack' % (
                name, version['id'], human(version.get('size', 0)),
                version.get('files', 0), (version.get('commit') or '?')[:10],
                fetched - started, time.time() - fetched))
        return not failed
    log('%s: no usable version in the store' % name)
    return not failed


def drop_versions(store, versions, names):
    for version in versions:
        for part in version['parts']:
            if part['name'] in names:
                store.delete(part['name'])
                names.discard(part['name'])


def collect_stale(entry, manifest, names):
    """Parts of this entry no version refers to, older than its newest
    version: leftovers of an interrupted save. Newer ones are left alone -
    they may be another run's save in progress."""
    versions = manifest['entries'].get(entry, {}).get('versions', [])
    kept = {v['id'] for v in versions}
    newest = versions[0]['id'] if versions else None
    stale = []
    for name in sorted(names):
        match = PART_NAME.match(name)
        if match and match.group('entry') == entry \
                and match.group('id') not in kept \
                and newest and match.group('id') < newest:
            stale.append(name)
    return stale


def save_one(store, name, args, state, scratch, zstd):
    paths = entry_paths(name, args.root, args.libraries, args.tree)
    if not paths:
        log('%s: nothing on disk to save' % name)
        return
    print_paths = paths if len(paths) <= 4 else paths[:3] + ['...']
    started = time.time()
    fp, files, total = fingerprint(args.root, paths)
    manifest = load_manifest(store, scratch)
    versions = manifest['entries'].get(name, {}).get('versions', [])
    newest = versions[0] if versions else None
    if not args.always and newest and newest.get('fingerprint') == fp \
            and (not salted(name) or newest.get('salt') == args.salt):
        state[name] = newest['id']
        log('%s: unchanged since %s, not uploaded' % (name, newest['id']))
        return
    if args.dry_run:
        log('%s: %s, would upload %d files (%s): %s' % (
            name, 'changed' if newest else 'new', files, human(total),
            ', '.join(print_paths)))
        return
    vid = new_id()
    directory = tempfile.mkdtemp(prefix='save-', dir=scratch)
    try:
        parts = pack(args.root, paths, directory, '%s-%s' % (name, vid),
                     args.part_size, zstd)
        packed = time.time()
        store.ensure()
        uploaded = []
        try:
            for part in parts:
                store.upload([os.path.join(directory, part['name'])])
                uploaded.append(part['name'])
        except Exception:
            for part_name in uploaded:
                try:
                    store.delete(part_name)
                except Exception as error:  # noqa: BLE001 - best effort
                    log('could not delete %s: %s' % (part_name, error))
            raise
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    version = {
        'id': vid,
        'created': datetime.datetime.now(datetime.timezone.utc).strftime(
            '%Y-%m-%dT%H:%M:%SZ'),
        'commit': os.environ.get('GITHUB_SHA'),
        'run': run_url(),
        'parts': parts,
        'paths': paths,
        'size': sum(p['size'] for p in parts),
        'files': files,
        'bytes': total,
        'fingerprint': fp,
        'machine': machine(zstd),
    }
    if salted(name) and args.salt is not None:
        version['salt'] = args.salt
    if args.built_against:
        version['built_against'] = {
            dep: state.get(dep) for dep in args.built_against.split(',') if dep}
    manifest = load_manifest(store, scratch)
    entry = manifest['entries'].setdefault(name, {'versions': []})
    entry['versions'].insert(0, version)
    dropped = entry['versions'][KEEP:]
    del entry['versions'][KEEP:]
    write_manifest(store, manifest, scratch)
    names = set(store.assets())
    drop_versions(store, dropped, names)
    for stale in collect_stale(name, manifest, names):
        store.delete(stale)
    state[name] = vid
    log('%s: saved %s - %d files, %s packed to %s in %d part(s); %.0fs '
        'pack, %.0fs upload' % (
            name, vid, files, human(total), human(version['size']),
            len(parts), packed - started, time.time() - packed))


def cmd_restore(args, store, scratch):
    zstd = find_zstd(args.zstd)
    state = read_state(args.state)
    manifest = load_manifest(store, scratch)
    os.makedirs(args.root, exist_ok=True)
    clean = True
    for name in args.entries:
        clean = restore_one(store, manifest, name, args, state, scratch,
                            zstd) and clean
        write_state(args.state, state)
    if not clean:
        # The step fails, visibly, but the workflow lets the build go on and
        # rebuild what could not be restored.
        sys.exit(1)


def cmd_save(args, store, scratch):
    zstd = find_zstd(args.zstd)
    state = read_state(args.state)
    for name in args.entries:
        save_one(store, name, args, state, scratch, zstd)
        write_state(args.state, state)


def cmd_status(args, store, scratch):
    manifest = load_manifest(store, scratch)
    assets = store.assets()
    if not manifest['entries']:
        log('the store holds no entries')
    for name in sorted(manifest['entries']):
        for version in manifest['entries'][name].get('versions', []):
            present = all(p['name'] in assets for p in version['parts'])
            log('%-20s %s  %s  commit %s  %s in %d part(s)%s' % (
                name, version['id'], version.get('created', '?'),
                (version.get('commit') or '?')[:10],
                human(version.get('size', 0)), len(version['parts']),
                '' if present else '  PARTS MISSING'))
    tools = sorted(n for n in assets if n.startswith('tool-'))
    for name in tools:
        log('%-20s %s' % ('tool', name))


def cmd_drop(args, store, scratch):
    manifest = load_manifest(store, scratch)
    names = set(store.assets())
    for name in args.entries:
        versions = manifest['entries'].pop(name, {}).get('versions', [])
        write_manifest(store, manifest, scratch)
        drop_versions(store, versions, names)
        for stale in [n for n in sorted(names)
                      if PART_NAME.match(n)
                      and PART_NAME.match(n).group('entry') == name]:
            store.delete(stale)
        log('%s: dropped %d version(s)' % (name, len(versions)))


def cmd_put(args, store, scratch):
    present = store.assets()
    todo = [p for p in args.files if os.path.basename(p) not in present]
    for path in args.files:
        if path not in todo:
            log('%s: already in the store' % os.path.basename(path))
    if todo:
        store.ensure()
        for path in todo:
            store.upload([path])
            log('%s: uploaded (%s)' % (os.path.basename(path),
                                        human(os.path.getsize(path))))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--store', required=True,
                        help='gh:OWNER/REPO:TAG or dir:PATH')
    parser.add_argument('--root', default='.',
                        help='the build root the entries live under')
    parser.add_argument('--libraries', default='Libraries/win64')
    parser.add_argument('--tree', default='tg/out')
    parser.add_argument('--zstd')
    parser.add_argument('--state', help='JSON file shared by one job\'s steps')
    parser.add_argument('--scratch', help='directory for downloads and parts')
    parser.add_argument('--salt', help='out/ salt; versions with another are '
                        'never restored')
    parser.add_argument('--built-against', help='entries, comma separated, '
                        'whose versions this save records as its inputs')
    parser.add_argument('--always', action='store_true',
                        help='save even if nothing changed')
    parser.add_argument('--dry-run', action='store_true',
                        help='say what save would upload, upload nothing')
    parser.add_argument('--part-size', type=int, default=PART_SIZE)
    sub = parser.add_subparsers(dest='command', required=True)
    for command in ('restore', 'save', 'drop'):
        sub.add_parser(command).add_argument('entries', nargs='+')
    sub.add_parser('status')
    sub.add_parser('put').add_argument('files', nargs='+')
    args = parser.parse_args()
    # The real path, not a link to it: bsdtar refuses to extract through a
    # symlink ("Cannot extract through symlink \\?\C:\b"), and the build
    # reaches its root through one - every restore of the first rehearsal,
    # run 39048108184, failed that way.
    args.root = os.path.realpath(os.path.abspath(args.root))
    store = open_store(args.store)
    base = args.scratch or os.environ.get('RUNNER_TEMP') or tempfile.gettempdir()
    os.makedirs(base, exist_ok=True)
    scratch = tempfile.mkdtemp(prefix='build-store-', dir=base)
    try:
        {'restore': cmd_restore, 'save': cmd_save, 'status': cmd_status,
         'drop': cmd_drop, 'put': cmd_put}[args.command](args, store, scratch)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == '__main__':
    main()
