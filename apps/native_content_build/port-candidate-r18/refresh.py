#!/usr/bin/env python3
"""Rebuild manifest.json from the overlay that is actually on disk.

Every overlay file is recorded with the digest of the tree it was taken from (the shared port checkout, the pinned checkout, or the
composed view) and with the digest it has now. A file whose two digests differ is marked edited, so the candidate's diff against the
base is exactly the list of edits, and the base identities are pinned so `apply.py` can refuse a base that moved underneath it.

usage: refresh.py [--note <path> <text>]
"""
import argparse
import hashlib
import json
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
MANIFEST = HERE / 'manifest.json'
OVERLAY = HERE / 'overlay'
PORT = Path('/mnt/f/GTOS-Chromium/sources/chromium-gtos/src')
FULL = Path('/mnt/f/GTOS-Chromium/sources/chromium-full/src')
VIEW = Path('/var/tmp/gtos-dsh-154-mainline/view/root')
ORIGINS = (VIEW, PORT, FULL)


def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def origin_of(rel):
    for origin in ORIGINS:
        candidate = origin / rel
        if candidate.is_file():
            return candidate
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--note', nargs=2, action='append', default=[], metavar=('PATH', 'TEXT'))
    args = parser.parse_args()
    previous = json.loads(MANIFEST.read_text()) if MANIFEST.exists() else {'files': [], 'base': {}}
    notes = {entry['path']: entry.get('note', '') for entry in previous.get('files', [])}
    for path, text in args.note:
        notes[path] = text
    files, problems = [], []
    for path in sorted(p for p in OVERLAY.rglob('*') if p.is_file()):
        rel = path.relative_to(OVERLAY).as_posix()
        origin = origin_of(rel)
        if origin is None:
            problems.append('no origin for ' + rel)
            continue
        origin_digest = digest(origin)
        overlay_digest = digest(path)
        stat = origin.stat()
        files.append({
            'path': rel, 'origin': str(origin), 'origin_bytes': stat.st_size, 'origin_sha256': origin_digest,
            'origin_mtime_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime(stat.st_mtime)),
            'overlay_bytes': path.stat().st_size, 'overlay_sha256': overlay_digest,
            'edited': origin_digest != overlay_digest,
            'shared_tree': str(origin).startswith('/mnt/f/GTOS-Chromium/sources/'),
            'note': notes.get(rel, ''),
        })
    manifest = {
        'task': 'GTOS-DSH-154', 'kind': 'private_port_candidate', 'round': 'r18',
        'purpose': 'an overlay the packet owns, so port edits are made here and never in the shared checkouts',
        'base': {'port': str(PORT), 'full': str(FULL), 'view': str(VIEW)},
        'refreshed_at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
        'files': files, 'problems': problems,
    }
    MANIFEST.write_text(json.dumps(manifest, indent=1) + '\n')
    print(json.dumps({'files': len(files), 'edited': [f['path'] for f in files if f['edited']], 'problems': problems}, indent=1))


if __name__ == '__main__':
    main()
