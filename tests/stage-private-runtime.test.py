#!/usr/bin/env python3
# GTOS-DSH-110 test for the production source-view staging module.
#
# The suite imports the pinned production module by path and calls its real functions. It does not re-implement the
# scan, the pin check or the reserved-key rule. Every fixture it creates lives under the TEST_ONLY root named in the
# control, which the caller keeps inside the Linux work root and separate from any business wave.

import hashlib
import importlib.util
import json
import os
import stat
import sys

assertions = 0
failures = 0


def check(value, label):
    global assertions, failures
    assertions += 1
    if not value:
        failures += 1
        sys.stderr.write('FAIL %s\n' % label)


def expect_refusal(action, label, needle=None):
    global assertions, failures
    assertions += 1
    try:
        action()
    except Exception as error:  # noqa: BLE001
        text = '%s: %s' % (type(error).__name__, error)
        if needle is not None and needle not in text:
            failures += 1
            sys.stderr.write('FAIL %s (wrong reason: %s)\n' % (label, text))
        return
    failures += 1
    sys.stderr.write('FAIL %s (did not refuse)\n' % label)


def sha_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def main(control_path):
    with open(control_path, 'r', encoding='utf-8') as handle:
        control = json.load(handle)

    # ---- the subject is the real production module, pinned before import ----------------------------------------
    module_path = control['module']['path']
    with open(module_path, 'rb') as handle:
        module_bytes = handle.read()
    check(len(module_bytes) == control['module']['bytes'], 'module byte count matches its pin')
    check(hashlib.sha256(module_bytes).hexdigest() == control['module']['sha256'],
          'module sha256 matches its pin')
    spec = importlib.util.spec_from_file_location('tested_private_staging', module_path)
    staging = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(staging)
    check(callable(staging.stage), 'the production module exposes stage()')
    check(callable(staging.assert_no_reserved_keys), 'the production module exposes the reserved-key guard')
    check(callable(staging.assert_linux_path), 'the production module exposes the Linux path guard')

    root = control['fixture_root']
    os.makedirs(root, exist_ok=True)
    source_dir = os.path.join(root, 'sources')
    os.makedirs(source_dir, exist_ok=True)
    work = os.path.join(root, 'work')
    os.makedirs(work, exist_ok=True)

    # ---- small real files, real pins --------------------------------------------------------------------------
    def make_source(name, payload):
        path = os.path.join(source_dir, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as handle:
            handle.write(payload)
        return {'logical': name, 'source': path, 'bytes': len(payload),
                'sha256': hashlib.sha256(payload).hexdigest()}

    alpha = make_source('src/alpha.cpp', b'alpha payload\n')
    beta = make_source('include/beta.h', b'beta payload with more bytes\n')
    files = [alpha, beta]

    # ---- a real successful staging ------------------------------------------------------------------------------
    result = staging.stage({'wave': 'wave-01', 'files': files}, work)
    check(result['ok'] is True, 'a valid request stages successfully')
    check(result['files'] == 2, 'both files were staged')
    check(result['bytes'] == alpha['bytes'] + beta['bytes'], 'the byte total is exact')
    destination = result['destination']
    check(destination == os.path.join(work, 'wave-01'), 'the destination is the claimed wave')

    for entry, payload in ((alpha, b'alpha payload\n'), (beta, b'beta payload with more bytes\n')):
        target = os.path.join(destination, *entry['logical'].split('/'))
        check(os.path.exists(target), 'staged file exists: %s' % entry['logical'])
        with open(target, 'rb') as handle:
            landed = handle.read()
        check(landed == payload, 'staged bytes equal the source: %s' % entry['logical'])
        check(sha_file(target) == entry['sha256'], 'staged hash equals the pin: %s' % entry['logical'])
        info = os.lstat(target)
        check(stat.S_ISREG(info.st_mode), 'staged entry is an ordinary file: %s' % entry['logical'])
        check(not stat.S_ISLNK(info.st_mode), 'staged entry is not a symlink: %s' % entry['logical'])
        check(info.st_nlink == 1, 'staged entry has nlink 1: %s' % entry['logical'])
        check(info.st_size == entry['bytes'], 'staged entry size is exact: %s' % entry['logical'])
    inodes = {os.lstat(os.path.join(destination, *e['logical'].split('/'))).st_ino for e in files}
    check(len(inodes) == 2, 'the two staged files have distinct inodes')

    # ---- an existing wave refuses and keeps its bytes -----------------------------------------------------------
    before = sha_file(os.path.join(destination, 'src/alpha.cpp'))
    expect_refusal(lambda: staging.stage({'wave': 'wave-01', 'files': files}, work),
                   'an existing wave refuses', 'already exists')
    check(sha_file(os.path.join(destination, 'src/alpha.cpp')) == before,
          'the refused wave keeps its content byte for byte')
    expect_refusal(lambda: staging.stage({'wave': 'wave-01', 'files': [alpha]}, work),
                   'a second attempt on the same wave also refuses', 'already exists')
    check(sha_file(os.path.join(destination, 'src/alpha.cpp')) == before,
          'the content survives the second refusal too')

    # ---- a fresh independent wave succeeds ---------------------------------------------------------------------
    second = staging.stage({'wave': 'wave-02', 'files': files}, work)
    check(second['ok'] is True and second['destination'] != destination,
          'a different wave stages into its own directory')
    check(os.path.exists(os.path.join(work, 'wave-01', 'src/alpha.cpp')),
          'the first wave is untouched by the second')

    # ---- the reserved-key rule, as a real failure --------------------------------------------------------------
    expect_refusal(lambda: staging.assert_no_reserved_keys({'require': 'x'}),
                   'a top-level require key refuses', 'reserved control key')
    expect_refusal(lambda: staging.assert_no_reserved_keys({'request': {'digest': 1}}),
                   'a nested digest key refuses', 'reserved control key')
    expect_refusal(lambda: staging.assert_no_reserved_keys({'list': [{'main': 1}]}),
                   'a reserved key inside a list refuses', 'reserved control key')
    staging.assert_no_reserved_keys({'request': {'wave': 'wave-03', 'note': 'fine'}})
    check(True, 'an ordinary control passes the reserved-key guard')
    # The 109 shape: a control whose top level carried `require` and whose helper bound it over the function.
    expect_refusal(lambda: staging.stage({'wave': 'wave-09', 'files': files, 'require': 'x'}, work),
                   'stage refuses a request carrying a reserved key', 'reserved control key')

    # ---- the Linux path rule ----------------------------------------------------------------------------------
    expect_refusal(lambda: staging.assert_linux_path('F:/GTOS-Chromium/x', 'source'),
                   'a Windows path refuses', 'absolute Linux path')
    expect_refusal(lambda: staging.assert_linux_path('relative/x', 'source'),
                   'a relative path refuses', 'absolute Linux path')
    expect_refusal(lambda: staging.assert_linux_path('/var/tmp/a\\b', 'source'),
                   'a backslash refuses', 'backslash')
    check(staging.assert_linux_path('/var/tmp/a/b', 'source') == '/var/tmp/a/b',
          'a canonical Linux path is accepted')
    # A request whose source is still a Windows path must refuse before any copy.
    windows_source = dict(alpha)
    windows_source['logical'] = 'src/windows.cpp'
    windows_source['source'] = 'F:/GTOS-Chromium/artifacts/x/src/alpha.cpp'
    expect_refusal(lambda: staging.stage({'wave': 'wave-08', 'files': [windows_source]}, work),
                   'a request carrying a Windows source refuses', 'absolute Linux path')
    check(not os.path.exists(os.path.join(work, 'wave-08')),
          'the refused request created no wave directory')

    # ---- pin, duplicate, escape and bound rules ---------------------------------------------------------------
    bad_pin = dict(alpha)
    bad_pin['logical'] = 'src/badpin.cpp'
    bad_pin['sha256'] = 'f' * 64
    expect_refusal(lambda: staging.stage({'wave': 'wave-04', 'files': [bad_pin]}, work),
                   'a wrong pin refuses', 'pin mismatch')
    check(not os.path.exists(os.path.join(work, 'wave-04')), 'the wrong-pin attempt created no directory')

    duplicate = [dict(alpha), dict(alpha)]
    expect_refusal(lambda: staging.stage({'wave': 'wave-05', 'files': duplicate}, work),
                   'a duplicate logical identity refuses', 'duplicate logical identity')

    escaping = dict(alpha)
    escaping['logical'] = '../escape.cpp'
    expect_refusal(lambda: staging.stage({'wave': 'wave-06', 'files': [escaping]}, work),
                   'a traversing logical identity refuses', 'must be safe')
    absolute = dict(alpha)
    absolute['logical'] = '/etc/passwd'
    expect_refusal(lambda: staging.stage({'wave': 'wave-06', 'files': [absolute]}, work),
                   'an absolute logical identity refuses', 'must be relative')

    empty = staging.stage({'wave': 'wave-07', 'files': []}, work) if False else None
    expect_refusal(lambda: staging.stage({'wave': 'wave-07', 'files': []}, work),
                   'an empty file list refuses', 'file count bound')

    expect_refusal(lambda: staging.stage({'wave': 'nope', 'files': files}, work),
                   'a malformed wave id refuses', 'wave identity')

    # ---- determinism across repeated calls --------------------------------------------------------------------
    digests = set()
    for index in range(8):
        wave = 'wave-1%d' % index
        staging.stage({'wave': wave, 'files': files}, work)
        digests.add(sha_file(os.path.join(work, wave, 'src/alpha.cpp')))
    check(len(digests) == 1, 'eight independent stagings produce one identical hash')
    check(digests.pop() == alpha['sha256'], 'the repeated hash equals the source pin')

    report = {
        'task': 'GTOS-DSH-110',
        'kind': 'staging_unit',
        'assertions': assertions,
        'failures': failures,
        'module': control['module'],
        'fixtures_are_test_only': True,
        'business_wave_touched': False,
        'runtime_or_guest_executed': False,
        'ok': failures == 0,
    }
    with open(control['result_path'], 'w', encoding='utf-8', newline='\n') as handle:
        json.dump(report, handle, indent=2, sort_keys=True)
        handle.write('\n')
    print(json.dumps({'ok': report['ok'], 'assertions': assertions, 'failures': failures}))
    return 0 if failures == 0 else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
