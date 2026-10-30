#!/usr/bin/env python3
"""Apply every port-candidate patch to its origin and compare the result with the manifest.

The candidate is committed as patches, because that is what a reviewer reads. This check closes the gap between the patch and the
bytes that were measured: for each edited file it copies the recorded origin into a scratch directory, applies the patch, and
compares the digest of the result with the overlay digest in the manifest. A patch that no longer reproduces the candidate, or a
manifest entry whose patch is missing, is a failure.

When the pinned checkouts are absent the check prints one skip line and exits 0: a checkout without them cannot answer the question,
and pretending otherwise would be worse than saying so.
"""
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CANDIDATE = ROOT / 'apps/native_content_build/port-candidate-r18'


def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def main():
    manifest = json.loads((CANDIDATE / 'candidate.json').read_text())
    edited = [entry for entry in manifest['files'] if entry['edited']]
    patches = {patch['name']: patch for patch in manifest['patches']}
    if not edited:
        print('skip port candidate: the manifest records no edited file')
        return 0
    missing = [entry['origin'] for entry in edited if not Path(entry['origin']).is_file()]
    if missing:
        print('skip port candidate: the pinned checkout is not present (%s)' % missing[0])
        return 0
    failures = 0
    if len(patches) != len(edited):
        print('FAIL the manifest records %d edited files but %d patches' % (len(edited), len(patches)), file=sys.stderr)
        failures += 1
    for entry in edited:
        name = entry['path'].replace('/', '__') + '.patch'
        patch = CANDIDATE / 'patches' / name
        if not patch.is_file():
            print('FAIL no patch for edited file %s (expected %s)' % (entry['path'], name), file=sys.stderr)
            failures += 1
            continue
        if patches.get(name, {}).get('sha256') != digest(patch):
            print('FAIL %s is not the patch the manifest recorded' % name, file=sys.stderr)
            failures += 1
        with tempfile.TemporaryDirectory() as scratch:
            target = Path(scratch) / 'file'
            shutil.copyfile(entry['origin'], target)
            result = subprocess.run(['patch', '-p1', '--silent', '--force', '--input', str(patch), str(target)],
                                    text=True, capture_output=True)
            if result.returncode != 0:
                print('FAIL %s does not apply to %s: %s' % (name, entry['origin'], result.stderr.strip()), file=sys.stderr)
                failures += 1
                continue
            applied = digest(target)
            if applied != entry['overlay_sha256']:
                print('FAIL %s applies cleanly but produces %s, not the recorded %s'
                      % (name, applied, entry['overlay_sha256']), file=sys.stderr)
                failures += 1
            else:
                print('ok %s reproduces %s' % (entry['path'], entry['overlay_sha256'][:16]))
    if failures:
        print('port candidate: %d failure(s)' % failures, file=sys.stderr)
        return 1
    print('PASS port candidate: %d patch(es) reproduce the recorded bytes exactly' % len(edited))
    return 0


if __name__ == '__main__':
    sys.exit(main())
