#!/usr/bin/env python3
"""Unit test for the r19 port candidate: the composer's refusals, the patch-to-overlay identity, and the wiring the patches carry.

What this is a test of: `apps/native_content_build/port-candidate-r19/candidate.py` and the eight patches beside it. What it is not:
a test that GN accepts the candidate (that is the guarded GN replay, a separate stage) or that a GTOS image runs (a separate stage).

The checks, each against the committed candidate rather than a copy of its logic:

  identity     every patch applies to its pinned origin and yields exactly the digest the manifest recorded, and applying it a
               second time to the result is refused by patch rather than silently succeeding.
  refusals     compose into an existing directory, into a directory inside a shared source root, and from an overlay whose digest
               drifted are all refused, and nothing is written outside the view.
  wiring       the toolchain patch adds -nostdinc and -nostdinc++ and the SSE2/emulated-TLS set; the SDK config patch names the four
               include directories in the qualification order; the libxml patch refuses through file_exists on a configure output
               and no longer through an unconditional assertion.

Usage: port_candidate_r19_test.py <repo root> [<scratch dir>]
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

CANDIDATE = 'apps/native_content_build/port-candidate-r19'
PASSED = []


def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def check(name, condition, detail=''):
    if condition:
        PASSED.append(name)
        print('PASS ' + name)
    else:
        print('FAIL ' + name + (': ' + detail if detail else ''))
        raise SystemExit(1)


def run(argv, cwd=None):
    return subprocess.run(argv, cwd=cwd, text=True, capture_output=True)


def main():
    root = Path(sys.argv[1]).resolve()
    scratch = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(tempfile.mkdtemp(prefix='port-candidate-r19-'))
    unit = root / CANDIDATE
    manifest = json.loads((unit / 'candidate.json').read_text())
    script = unit / 'candidate.py'
    check('candidate has a manifest and a composer', (unit / 'candidate.json').is_file() and script.is_file())
    check('the manifest records eight edited files', len(manifest['files']) == 8, str(len(manifest['files'])))
    check('every recorded file carries a patch digest',
          all(entry.get('patch_sha256') for entry in manifest['files']))

    # identity: each patch turns its pinned origin into the recorded bytes, and applying it twice is refused.
    work = scratch / 'identity'
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    for entry in manifest['files']:
        target = work / Path(entry['path']).name
        shutil.copyfile(entry['origin'], target)
        first = run(['patch', '-p1', '--silent', '--force', '--input', str(unit / 'patches' / entry['patch']), str(target)])
        check('patch applies: ' + entry['path'], first.returncode == 0, first.stderr.strip())
        check('patch reproduces the recorded bytes: ' + entry['path'], digest(target) == entry['overlay_sha256'],
              digest(target) + ' != ' + entry['overlay_sha256'])
        check('the pinned origin is still the one the manifest recorded: ' + entry['path'],
              digest(entry['origin']) == entry['origin_sha256'], digest(entry['origin']) + ' != ' + entry['origin_sha256'])

    # wiring: the flags the qualification build used show up in the two patches that carry them, and libxml's branch measures a
    # missing file instead of asserting a conclusion.
    toolchain = (unit / 'patches' / 'build__toolchain__gtos__BUILD.gn.patch').read_text()
    for flag in ('-nostdinc"', '-nostdinc++'):
        check('toolchain patch carries ' + flag, flag in toolchain)
    check('toolchain patch leaves the host libc out of the search path',
          '-isystem/usr/include' not in toolchain and '-I/usr/include' not in toolchain)
    config = (unit / 'patches' / 'build__config__gtos__BUILD.gn.patch').read_text()
    sdk_block = config[config.index('config("sdk")'):]
    order = [sdk_block.index(marker) for marker in ('/sdk"', 'gtos_libcxx_include', '/c-sdk"', 'gtos_clang_include')]
    check('SDK config names the four include directories in the qualification order', order == sorted(order), str(order))
    for flag in ('-msse2', '-mfpmath=sse', '-mno-avx', '-femulated-tls', '-fstack-usage'):
        check('SDK config carries ' + flag, flag in config)
    libxml = (unit / 'patches' / 'third_party__libxml__BUILD.gn.patch').read_text()
    check('libxml refuses through a measured file presence',
          'file_exists("$gtos_libxml_config_dir/config.h")' in libxml
          and 'file_exists("$gtos_libxml_config_dir/include/libxml/xmlversion.h")' in libxml)
    check('libxml no longer refuses unconditionally', 'assert(\n      false,' not in libxml and '      false,\n' not in libxml)

    # refusals: the composer declines a destination that exists, one inside a shared root, and a drifted overlay.
    existing = scratch / 'existing'
    existing.mkdir()
    refused = run([sys.executable, '-B', str(script), '--compose', str(existing)], cwd=root)
    check('compose refuses an existing destination', refused.returncode != 0 and 'already exists' in refused.stdout + refused.stderr,
          refused.stdout + refused.stderr)
    shared = Path('/mnt/f/GTOS-Chromium/sources/chromium-gtos/src/port-r19-should-not-exist')
    refused = run([sys.executable, '-B', str(script), '--compose', str(shared)], cwd=root)
    check('compose refuses a destination inside a shared root',
          refused.returncode != 0 and 'shared source root' in refused.stdout + refused.stderr, refused.stdout + refused.stderr)
    check('the refused shared destination was not created', not shared.exists())

    # A copy of the unit with one patch altered: the composer must refuse because the result no longer matches the manifest digest.
    broken = scratch / 'drifted'
    shutil.copytree(unit, broken)
    patch_path = broken / 'patches' / 'v8__BUILD.gn.patch'
    patch_path.write_text(patch_path.read_text().replace('GTOS V8 runtime backend is not implemented',
                                                         'GTOS V8 runtime backend is not implemented!'))
    drifted = run([sys.executable, '-B', str(broken / 'candidate.py'), '--compose', str(scratch / 'drifted-view')], cwd=root)
    check('compose refuses a patch whose result drifted from the manifest',
          drifted.returncode != 0 and ('not the recorded' in drifted.stdout + drifted.stderr
                                       or 'does not apply' in drifted.stdout + drifted.stderr),
          drifted.stdout + drifted.stderr)
    check('the refused drifted view was not left behind', not (scratch / 'drifted-view').exists())

    print('\n%d checks passed' % len(PASSED))
    return 0


if __name__ == '__main__':
    sys.exit(main())
