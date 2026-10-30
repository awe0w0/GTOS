#!/usr/bin/env python3
"""Compose a build root from the pinned checkouts and this candidate's overlay.

The overlay is the only place a GTOS port edit is made. Composition links every other entry of the pinned checkouts, so nothing is
copied out of the shared trees and nothing is written into them: the script refuses any destination under a shared source root, and
it refuses to compose at all if a shared file it was captured from has since changed, because then the candidate would no longer be
the thing that was reviewed.

usage: apply.py --view <new directory> [--check-only]
writes: <new directory>/... and <new directory>/../applied-<name>.json
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
MANIFEST = HERE / 'manifest.json'
PATCHES = HERE / 'patches'
SHARED_ROOTS = (Path('/mnt/f/GTOS-Chromium/sources'),)


def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def under_shared(path):
    resolved = Path(os.path.realpath(path))
    for root in SHARED_ROOTS:
        if resolved == root or root in resolved.parents:
            return True
    return False


def check_base(manifest):
    """Refuse when a shared file the candidate was captured from has changed since it was captured."""
    problems = []
    for entry in manifest['files']:
        if entry['shared_tree']:
            current = Path(entry['origin'])
            if not current.is_file():
                problems.append('gone: ' + entry['origin'])
            elif digest(current) != entry['origin_sha256']:
                problems.append('changed: ' + entry['origin'])
    return problems


def materialize_parents(view, rel):
    """Make every directory above <rel> a real directory inside the view, replacing symlinks on the way.

    A composed view links most entries to the pinned checkouts. Writing an overlay file through one of those links would write into
    the shared tree it points at, which is the one thing this candidate exists to avoid. Each symlinked ancestor is therefore
    replaced by a real directory whose entries are links to the same children, and the walk continues inside the view.
    """
    current = view
    for part in rel.split('/')[:-1]:
        nxt = current / part
        if nxt.is_symlink():
            target = Path(os.path.realpath(nxt))
            if not target.is_dir():
                raise SystemExit('refusing to apply into %s: %s is a leaf link' % (rel, nxt))
            staging = current / ('.materialize-' + part)
            if staging.exists():
                shutil.rmtree(staging)
            staging.mkdir()
            for child in sorted(os.listdir(target)):
                os.symlink(str(target / child), str(staging / child))
            nxt.unlink()
            os.rename(staging, nxt)
        nxt.mkdir(exist_ok=True)
        current = nxt
    return current


def compose(manifest, view):
    port = Path(manifest['base']['port'])
    full = Path(manifest['base']['full'])
    # Only the edited files become real files in the view. The rest of the manifest records what was read, so they are composed as
    # links like everything else: skipping them would leave the view without the files the candidate did not change.
    overlay_paths = {entry['path'] for entry in manifest['files'] if entry.get('edited')}
    overlay_dirs = set()
    for rel in overlay_paths:
        parent = os.path.dirname(rel)
        while parent:
            overlay_dirs.add(parent)
            parent = os.path.dirname(parent)

    links = 0
    real_files = 0
    real_dirs = 0

    def link(src, dst):
        nonlocal links
        if not os.path.lexists(dst):
            os.symlink(src, dst)
            links += 1

    def ensure_dir(path):
        nonlocal real_dirs
        if not os.path.isdir(path):
            os.makedirs(path, exist_ok=True)
            real_dirs += 1

    view.mkdir(parents=True, exist_ok=False)
    # 1. Every file the port checkout carries, linked, except the paths this overlay replaces.
    port_files, port_dirs = [], set()
    for base, dirs, files in os.walk(port):
        rel = os.path.relpath(base, port)
        if rel != '.':
            port_dirs.add(rel)
        for name in files:
            relfile = name if rel == '.' else rel + '/' + name
            if relfile not in overlay_paths:
                port_files.append(relfile)
    for rel in sorted(port_files):
        parent = os.path.dirname(rel)
        if parent:
            ensure_dir(view / parent)
        link(port / rel, view / rel)
    for rel in sorted(port_dirs):
        if rel in overlay_dirs or any(rel == d or rel.startswith(d + '/') for d in overlay_paths):
            continue
        ensure_dir(view / rel)
    # 2. Everything the port does not carry comes from the pinned checkout.
    for rel in sorted(port_dirs) + ['']:
        target = view / rel if rel else view
        source = full / rel if rel else full
        if not source.is_dir():
            continue
        carried = set(os.listdir(target)) if target.is_dir() else set()
        for name in sorted(os.listdir(source)):
            if name in carried:
                continue
            candidate_rel = name if not rel else rel + '/' + name
            if candidate_rel in overlay_paths or candidate_rel in overlay_dirs:
                continue
            link(str(source / name), str(target / name))
    # 3. Top-level entries only the pinned checkout has.
    for name in sorted(os.listdir(full)):
        if not os.path.lexists(view / name):
            link(str(full / name), str(view / name))
    # 4. The candidate itself, as real files: each patch is applied to a copy of the file it was generated from, inside the view.
    applied = []
    shared_writes = []
    for entry in manifest['files']:
        if not entry.get('edited'):
            continue
        patch = PATCHES / (entry['path'].replace('/', '__') + '.patch')
        if not patch.is_file():
            raise SystemExit('refusing to compose: no patch for edited file ' + entry['path'])
        parent = materialize_parents(view, entry['path'])
        target = parent / os.path.basename(entry['path'])
        view_real = Path(os.path.realpath(view))
        if target.is_symlink():
            # The candidate replaces a path the composition linked to a shared tree. Removing that link and writing a real file is the
            # whole point; writing through it would put the edit in the shared tree instead.
            resolved = Path(os.path.realpath(target))
            if resolved == view_real or view_real in resolved.parents:
                raise SystemExit('refusing to apply over a link inside the view: ' + str(target))
            target.unlink()
        real_parent = Path(os.path.realpath(parent))
        if under_shared(real_parent) or (real_parent != view_real and view_real not in real_parent.parents):
            shared_writes.append(str(real_parent))
            raise SystemExit('refusing to write into %s: it is not inside the view' % real_parent)
        # The copy starts from the file the patch was generated against, so the patch applies to exactly the bytes it names.
        shutil.copyfile(entry['origin'], target)
        patched = subprocess.run(['patch', '-p1', '--silent', '--force', '--input', str(patch), str(target)],
                                 text=True, capture_output=True)
        if patched.returncode != 0:
            raise SystemExit('refusing to compose: %s does not apply to %s: %s'
                             % (patch.name, entry['origin'], patched.stderr.strip()))
        real_files += 1
        recorded = digest(target)
        if recorded != entry['overlay_sha256']:
            raise SystemExit('refusing to compose: %s produced %s, not the recorded %s'
                             % (entry['path'], recorded, entry['overlay_sha256']))
        applied.append({'path': entry['path'], 'sha256': recorded, 'bytes': target.stat().st_size,
                        'origin': entry['origin'], 'origin_sha256': entry['origin_sha256'],
                        'patch': patch.name, 'patch_sha256': digest(patch),
                        'matches_recorded': True,
                        'real_path': str(Path(os.path.realpath(target)))})
    return {'links': links, 'real_files': real_files, 'real_dirs': real_dirs, 'applied': applied,
            'shared_writes': shared_writes}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--view', type=Path, required=True, help='new directory to compose into; must not exist')
    parser.add_argument('--check-only', action='store_true', help='verify the shared base only, compose nothing')
    args = parser.parse_args()
    manifest = json.loads(MANIFEST.read_text())
    problems = check_base(manifest)
    if problems:
        sys.stderr.write('candidate refused: the shared base moved\n  ' + '\n  '.join(problems) + '\n')
        return 2
    if under_shared(args.view):
        sys.stderr.write('candidate refused: %s is inside a shared source root\n' % args.view)
        return 2
    if args.check_only:
        print(json.dumps({'base_ok': True, 'files': len(manifest['files'])}, indent=1))
        return 0
    summary = compose(manifest, args.view.resolve())
    summary.update({'view': str(args.view.resolve()), 'at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
                    'manifest_sha256': digest(MANIFEST)})
    record = args.view.resolve().parent / ('applied-' + args.view.resolve().name + '.json')
    record.write_text(json.dumps(summary, indent=1) + '\n')
    print(json.dumps({k: v for k, v in summary.items() if k != 'applied'}, indent=1))
    return 0


if __name__ == '__main__':
    sys.exit(main())
