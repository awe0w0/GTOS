#!/usr/bin/env python3
"""Build and replay the r19 GTOS port candidate.

The candidate is the set of control bytes a composed build root needs in order to describe a GTOS target: the six files the r18
view carried (the V8 refusal switch, the crash-key default, the content/test fixture defect, libxml's GTOS branch, `.gn`'s
interpreter supply and the root bring-up gate) plus the two files that tell the compiler where the freestanding headers are.

Two modes:

  --rebuild          Produce the candidate's own bytes for every edited file, store them under the round's private overlay
                     directory (never in a shared checkout), and write one patch per file plus `candidate.json`.

  --compose <dir>    Compose a build root from the pinned checkouts and the committed patches: link every entry of both
                     checkouts, then for each edited file copy the pinned origin into the view, apply that file's patch to the
                     copy, and refuse unless the result has the digest the manifest recorded.

Composition never writes through a symbolic link: each replaced link is removed first, and every write target is checked by
realpath to be inside the view. The caller runs this under the original guard with both checkouts read-only, so a mistake here is
a failed write rather than an edited shared tree.
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
PATCHES = HERE / 'patches'
MANIFEST = HERE / 'candidate.json'
OVERLAY = Path('/mnt/f/GTOS-Chromium/artifacts/native-ci-v8-content-execution-resume-20261029-a/port-candidate-154-r19/overlay')

PORT = Path('/mnt/f/GTOS-Chromium/sources/chromium-gtos/src')
FULL = Path('/mnt/f/GTOS-Chromium/sources/chromium-full/src')
R18_VIEW = Path('/var/tmp/gtos-dsh-154-mainline/port-r18/view-root-07')
R18_PATCHES = Path('/mnt/f/GTOS-Chromium/artifacts/native-ci-v8-content-execution-resume-20261029-a/worktree-154'
                   '/apps/native_content_build/port-candidate-r18/patches')
SHARED_ROOTS = (Path('/mnt/f/GTOS-Chromium/sources'),)

SDK = '/mnt/f/GTOS-Chromium/artifacts/native-ci-v8-content-execution-resume-20261029-a/worktree-154/apps/native_v8_runtime'
CLANG_INCLUDE = '/mnt/f/GTOS-Chromium/toolchains/chromium-clang-llvm24-62397f8b-57/lib/clang/24/include'
LIBCXX_INCLUDE = str(FULL / 'third_party/libc++/src/include')


def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def under_shared(path):
    resolved = Path(os.path.realpath(path))
    return any(resolved == root or root in resolved.parents for root in SHARED_ROOTS)


def origin_of(rel):
    for base in (PORT, FULL):
        if (base / rel).is_file():
            return base / rel
    raise SystemExit('no pinned origin carries ' + rel)


def patch_name(rel):
    return rel.replace('/', '__') + '.patch'


# --- the two wiring edits -------------------------------------------------------------------------------------------------------
#
# One decision, in two files: the port describes a freestanding GTOS target but never says where its headers are, so every
# translation unit searched the host's /usr/include and failed on bits/libc-header-start.h. The qualified IA32 V8 build records
# the answer it used -- -nostdinc, the GTOS SDK's own headers, libc++'s headers, the clang resource directory, -msse2
# -mfpmath=sse, -femulated-tls -fstack-usage (native-v8-integration-20261011-a/wide-runtime-resume-30/build/toolchain.ninja).
# This replays that recipe as arguments, not as copied host flags.

SDK_CONFIG = '''# Copyright 2026 The Chromium Authors
# Use of this source code is governed by a BSD-style license in LICENSE.

# Freestanding header and flag supply for the GTOS target.
#
# Neither pinned checkout carries a libc. `third_party/libc++/src/include` has the C++ library headers and the reviewed GTOS SDK
# has the C ones, its type and macro headers, and the C++ config the runtime was qualified against. Nothing in the host's
# /usr/include belongs to this target, so the toolchain passes -nostdinc and every directory the compiler may read is named here.
#
# The paths are arguments so a later round can point this at a rebuilt SDK without editing the file. They are not guesses: they
# are the directories the qualified V8 build compiled against.
declare_args() {
  # Holds `sdk/` (libc++ config for GTOS) and `c-sdk/` (LLVM libc headers, types and macros).
  gtos_sdk_dir = ""
  # libc++ headers from the pinned checkout.
  gtos_libcxx_include = ""
  # The clang resource directory: stddef.h, stdint.h, stdarg.h and the compiler's own builtins.
  gtos_clang_include = ""
}

assert(gtos_sdk_dir != "", "Set gtos_sdk_dir to the reviewed GTOS SDK")
assert(gtos_libcxx_include != "", "Set gtos_libcxx_include to the pinned libc++ headers")
assert(gtos_clang_include != "", "Set gtos_clang_include to the clang resource include directory")

config("sdk") {
  assert(is_gtos)
  # The qualification build's order: GTOS SDK C++ config, libc++, GTOS SDK C headers, then the compiler's own headers.
  cflags = [
    "-isystem" + gtos_sdk_dir + "/sdk",
    "-isystem" + gtos_libcxx_include,
    "-isystem" + gtos_sdk_dir + "/c-sdk",
    "-isystem" + gtos_clang_include,
    # The GTOS userspace ABI the runtime was qualified against: IA32 with SSE2 for floating point, emulated TLS because the
    # target has no thread-pointer register contract of its own.
    "-msse2",
    "-mfpmath=sse",
    "-mno-sse3",
    "-mno-ssse3",
    "-mno-sse4",
    "-mno-avx",
    "-femulated-tls",
    "-fstack-usage",
  ]
}

config("platform") {
  assert(is_gtos)
  defines = [ "__GTOS__=1" ]
  include_dirs = [ "//" ]
  configs = [ ":sdk" ]
}
'''


def transform_config(text):
    if 'config("sdk")' in text:
        raise SystemExit('refusing to transform build/config/gtos/BUILD.gn twice')
    return SDK_CONFIG


def transform_toolchain(text):
    old_common = ('_common = "--target=$_triple -ffreestanding -fno-stack-protector -fno-pic -fno-pie '
                  '-fno-unwind-tables -fno-asynchronous-unwind-tables -mno-mmx -mno-sse -mno-sse2"')
    if old_common not in text:
        raise SystemExit('the toolchain _common line is not the one this transform expects')
    new_common = ('_common = "--target=$_triple -ffreestanding -fno-stack-protector -fno-pic -fno-pie '
                  '-fno-unwind-tables -fno-asynchronous-unwind-tables -mno-mmx -msse2 -mfpmath=sse -mno-sse3 '
                  '-mno-ssse3 -mno-sse4 -mno-avx -nostdinc"')
    text = text.replace(old_common, new_common)
    old_cxx = 'command = "$_clang -x c++ $_common -fno-exceptions -fno-rtti'
    if old_cxx not in text:
        raise SystemExit('the cxx tool command is not the one this transform expects')
    text = text.replace(old_cxx, 'command = "$_clang -x c++ $_common -nostdinc++ -fno-exceptions -fno-rtti')
    note = ('    # -nostdinc: the only headers this target may read are the ones //build/config/gtos:sdk names. Without it every\n'
            '    # translation unit searched the host\'s /usr/include and failed on bits/libc-header-start.h.\n')
    text = text.replace('    _common = ', note + '    _common = ', 1)
    return text


# libxml: r18's GTOS branch was `assert(false, "...no configure output...")`, which states a conclusion nobody measured. The GTOS
# branch itself is what the r18 patch adds, so this condition is written on top of that patch's result, not on top of upstream. The
# new branch names where a real configure output has to be and fails on the missing file -- a measurement a cross configure can
# satisfy, instead of a sentence that cannot.
LIBXML_OLD = '''} else if (is_gtos) {
  # GTOS is freestanding: there is no libc, no configure run against one, and therefore no OS-specific generated headers. The linux
  # directory beside this file is not a substitute: it is configure output for a libc GTOS does not have, and pointing a GTOS build
  # at it would compile against a platform that is not the one being built. Naming a directory that does not exist would only move
  # this failure from the scope error below to a missing include, so the file states the missing dependency where the directory
  # would have been named.
  assert(
      false,
      "GTOS has no libxml configure output: third_party/libxml/gtos/{config.h,include/libxml/xmlversion.h} would have to come from a real configure run against a GTOS libc, which this target does not have")
}
'''

LIBXML_NEW = '''} else if (is_gtos) {
  # libxml's build reads a configure result: config.h and include/libxml/xmlversion.h. A GTOS one has to come from libxml's own
  # configure run against the GTOS SDK, and this file does not invent it. gtos_libxml_config_dir names where that output has to be;
  # the check below fails on the missing file, which is a measurement, not an assumption.
  assert(gtos_libxml_config_dir != "", "Set gtos_libxml_config_dir to a GTOS libxml configure output")
  assert(
      file_exists("$gtos_libxml_config_dir/config.h") &&
          file_exists("$gtos_libxml_config_dir/include/libxml/xmlversion.h"),
      "GTOS libxml configure output is missing: $gtos_libxml_config_dir must hold config.h and include/libxml/xmlversion.h produced by libxml's own configure run against the GTOS SDK")
  os_include = gtos_libxml_config_dir
}
'''


def transform_libxml(text):
    straight = '"'
    if LIBXML_OLD not in text:
        raise SystemExit('the libxml GTOS branch is not the one this transform expects')
    text = text.replace(LIBXML_OLD, LIBXML_NEW)
    anchor = 'if (is_linux || is_chromeos || is_android || is_fuchsia) {\n'
    if anchor not in text:
        raise SystemExit('libxml BUILD.gn no longer starts its os_include chain the expected way')
    declare = 'declare_args() {\n  gtos_libxml_config_dir = ""\n}\n\n'
    assert straight in declare
    return text.replace(anchor, declare + anchor, 1)


TRANSFORMS = {
    'build/config/gtos/BUILD.gn': transform_config,
    'build/toolchain/gtos/BUILD.gn': transform_toolchain,
}

# Produced by applying the r18 patch first and then this round's condition: the GTOS branch does not exist upstream.
PATCHED_THEN_TRANSFORMED = {
    'third_party/libxml/BUILD.gn': ('third_party__libxml__BUILD.gn.patch', transform_libxml),
}

CARRIED = {
    '.gn': None,
    'BUILD.gn': None,
    'v8/BUILD.gn': 'v8__BUILD.gn.patch',
    'components/crash/core/common/BUILD.gn': 'components__crash__core__common__BUILD.gn.patch',
    'content/test/BUILD.gn': 'content__test__BUILD.gn.patch',
}


def rebuild():
    # A generation that never wrote its manifest is a half-attempt, and only that may be cleared; a generation with a manifest is
    # the reviewed candidate and is never rewritten. The attempt itself stays in the guard's evidence either way.
    if MANIFEST.exists():
        raise SystemExit('refusing to rebuild: %s already describes a finished generation' % MANIFEST)
    for path in (PATCHES, OVERLAY):
        if path.exists():
            shutil.rmtree(path)
        path.mkdir(parents=True)
    files = []
    for rel, r18_patch in sorted(CARRIED.items()):
        origin = origin_of(rel)
        out = OVERLAY / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        if r18_patch:
            shutil.copyfile(origin, out)
            done = subprocess.run(['patch', '-p1', '--silent', '--force', '--input', str(R18_PATCHES / r18_patch), str(out)],
                                  text=True, capture_output=True)
            if done.returncode != 0:
                raise SystemExit('%s does not apply: %s' % (r18_patch, done.stderr.strip()))
            if digest(out) != digest(R18_VIEW / rel):
                raise SystemExit('the r18 patch no longer reproduces the r18 view for ' + rel)
            how = 'pinned origin + the r18 patch, byte-identical to the r18 view'
        else:
            shutil.copyfile(R18_VIEW / rel, out)
            how = 'the r18 view bytes, which were the qualified frontier state and had no patch'
        files.append({'path': rel, 'origin': str(origin), 'origin_sha256': digest(origin), 'overlay_sha256': digest(out),
                      'bytes': out.stat().st_size, 'how': how})
    for rel, (r18_patch, transform) in sorted(PATCHED_THEN_TRANSFORMED.items()):
        origin = origin_of(rel)
        out = OVERLAY / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(origin, out)
        done = subprocess.run(['patch', '-p1', '--silent', '--force', '--input', str(R18_PATCHES / r18_patch), str(out)],
                              text=True, capture_output=True)
        if done.returncode != 0:
            raise SystemExit('%s does not apply: %s' % (r18_patch, done.stderr.strip()))
        if digest(out) != digest(R18_VIEW / rel):
            raise SystemExit('the r18 patch no longer reproduces the r18 view for ' + rel)
        out.write_text(transform(out.read_text()))
        files.append({'path': rel, 'origin': str(origin), 'origin_sha256': digest(origin), 'overlay_sha256': digest(out),
                      'bytes': out.stat().st_size,
                      'how': 'pinned origin + the r18 patch, then this round\'s condition replaces the unconditional refusal'})
    for rel, transform in sorted(TRANSFORMS.items()):
        origin = origin_of(rel)
        out = OVERLAY / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(transform(origin.read_text()))
        files.append({'path': rel, 'origin': str(origin), 'origin_sha256': digest(origin), 'overlay_sha256': digest(out),
                      'bytes': out.stat().st_size, 'how': 'pinned origin transformed by this generator'})
    for entry in files:
        rel = entry['path']
        name = patch_name(rel)
        done = subprocess.run(['diff', '-u', '--label', 'a/' + rel, '--label', 'b/' + rel,
                               entry['origin'], str(OVERLAY / rel)], text=True, capture_output=True)
        if done.returncode not in (0, 1):
            raise SystemExit('diff failed for ' + rel)
        (PATCHES / name).write_text(done.stdout)
        entry['patch'] = name
        entry['patch_sha256'] = digest(PATCHES / name)
        entry['edited'] = bool(done.stdout)
        if entry['edited'] and entry['overlay_sha256'] == entry['origin_sha256']:
            raise SystemExit('patch for %s is empty but the digests differ' % rel)
    manifest = {
        'task': 'GTOS-DSH-154',
        'round': 'r19',
        'what': 'the private GTOS port candidate: the r18 qualification bytes as replayable control files, plus the freestanding '
                'SDK wiring that tells the GTOS toolchain where its headers are',
        'base': {'port': str(PORT), 'full': str(FULL)},
        'sdk': {'dir': SDK, 'libcxx_include': LIBCXX_INCLUDE, 'clang_include': CLANG_INCLUDE},
        'carried_from': str(R18_VIEW),
        'files': files,
        'generated_at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
    }
    MANIFEST.write_text(json.dumps(manifest, indent=1) + '\n')
    print('wrote %d candidate files and %d patches' % (len(files), len(files)))
    for entry in files:
        print('  %s edits %-45s %s' % ('edited ' if entry['edited'] else 'carried', entry['path'], entry['overlay_sha256'][:16]))
    return 0


def materialize_parents(view, rel):
    current = view
    for part in rel.split('/')[:-1]:
        nxt = current / part
        if nxt.is_symlink():
            target = Path(os.path.realpath(nxt))
            if not target.is_dir():
                raise SystemExit('refusing to compose %s: %s is a leaf link' % (rel, nxt))
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


def compose(view):
    manifest = json.loads(MANIFEST.read_text())
    if view.exists():
        raise SystemExit('refusing to compose into %s: it already exists' % view)
    if under_shared(view):
        raise SystemExit('refusing to compose into %s: it is inside a shared source root' % view)
    moved = [entry['origin'] for entry in manifest['files'] if digest(entry['origin']) != entry['origin_sha256']]
    if moved:
        raise SystemExit('refusing to compose: a pinned origin moved since the manifest was written:\n  ' + '\n  '.join(moved))
    missing = [entry['patch'] for entry in manifest['files'] if not (PATCHES / entry['patch']).is_file()]
    if missing:
        raise SystemExit('refusing to compose: the candidate is missing patches:\n  ' + '\n  '.join(missing))
    view.mkdir(parents=True)
    try:
        return _lay_out(view, manifest)
    except BaseException:
        # A refusal must not leave a half-composed view behind: it looks like a usable root and its edited files are a
        # subset of the candidate. The refusal itself is what the record keeps.
        shutil.rmtree(view, ignore_errors=True)
        raise


def _lay_out(view, manifest):
    edited = [entry for entry in manifest['files'] if entry['edited']]
    edited_paths = {entry['path'] for entry in edited}
    replaced_dirs = set()
    for entry in edited:
        parent = os.path.dirname(entry['path'])
        while parent:
            replaced_dirs.add(parent)
            parent = os.path.dirname(parent)
    view_real = Path(os.path.realpath(view))
    links = 0
    port_files, port_dirs = [], set()
    for base, dirs, names in os.walk(PORT):
        rel = os.path.relpath(base, PORT)
        if rel != '.':
            port_dirs.add(rel)
        for name in names:
            relfile = name if rel == '.' else rel + '/' + name
            if relfile not in edited_paths:
                port_files.append(relfile)
    for rel in sorted(port_files):
        parent = os.path.dirname(rel)
        if parent:
            (view / parent).mkdir(parents=True, exist_ok=True)
        os.symlink(str(PORT / rel), str(view / rel))
        links += 1
    for rel in sorted(port_dirs):
        if rel in replaced_dirs:
            continue
        (view / rel).mkdir(parents=True, exist_ok=True)
    for rel in sorted(port_dirs) + ['']:
        source_dir = FULL / rel if rel else FULL
        target_dir = view / rel if rel else view
        if not source_dir.is_dir():
            continue
        carried = set(os.listdir(target_dir)) if target_dir.is_dir() else set()
        for name in sorted(os.listdir(source_dir)):
            if name in carried:
                continue
            candidate_rel = name if not rel else rel + '/' + name
            if candidate_rel in edited_paths or candidate_rel in replaced_dirs:
                continue
            os.symlink(str(source_dir / name), str(target_dir / name))
            links += 1
    for name in sorted(os.listdir(FULL)):
        if not os.path.lexists(view / name):
            os.symlink(str(FULL / name), str(view / name))
            links += 1
    applied = []
    for entry in edited:
        patch = PATCHES / entry['patch']
        if not patch.is_file():
            raise SystemExit('refusing to compose: no patch for ' + entry['path'])
        parent = materialize_parents(view, entry['path'])
        target = parent / os.path.basename(entry['path'])
        if target.is_symlink():
            resolved = Path(os.path.realpath(target))
            if resolved == view_real or view_real in resolved.parents:
                raise SystemExit('refusing to replace a link inside the view: ' + str(target))
            target.unlink()
        real_parent = Path(os.path.realpath(parent))
        if under_shared(real_parent) or (real_parent != view_real and view_real not in real_parent.parents):
            raise SystemExit('refusing to write into %s: it is not inside the view' % real_parent)
        shutil.copyfile(entry['origin'], target)
        done = subprocess.run(['patch', '-p1', '--silent', '--force', '--input', str(patch), str(target)],
                              text=True, capture_output=True)
        if done.returncode != 0:
            raise SystemExit('%s does not apply to %s: %s' % (entry['patch'], entry['origin'], done.stderr.strip()))
        got = digest(target)
        if got != entry['overlay_sha256']:
            raise SystemExit('refusing: %s composed to %s, not the recorded %s' % (entry['path'], got, entry['overlay_sha256']))
        applied.append({'path': entry['path'], 'sha256': got, 'bytes': target.stat().st_size,
                        'origin_sha256': entry['origin_sha256'], 'patch_sha256': entry['patch_sha256'],
                        'real_path': str(Path(os.path.realpath(target)))})
    summary = {'view': str(view_real), 'links': links, 'edited_files': len(applied), 'applied': applied,
               'at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), 'candidate_sha256': digest(MANIFEST)}
    (view.parent / ('composed-' + view.name + '.json')).write_text(json.dumps(summary, indent=1) + '\n')
    print(json.dumps({k: v for k, v in summary.items() if k != 'applied'}, indent=1))
    for entry in applied:
        print('  edited %s %s' % (entry['sha256'][:16], entry['path']))
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--rebuild', action='store_true', help='write the candidate overlay, patches and manifest')
    parser.add_argument('--compose', type=Path, help='compose a new build root at this path')
    args = parser.parse_args()
    if args.rebuild:
        return rebuild()
    if args.compose:
        return compose(args.compose.resolve())
    parser.error('choose --rebuild or --compose <dir>')


if __name__ == '__main__':
    sys.exit(main())
