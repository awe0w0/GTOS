#!/usr/bin/env python3
# GTOS-DSH-142 runtime identity reader.
#
# This module reads a published runtime tree and re-derives its identity from the filesystem. It is not the module that wrote
# the tree: the writer hashed each file as it copied it, and a writer reporting on itself is not a check. This one starts from
# the bytes on disk, inside the distribution, where POSIX mode, link count and inode are real values rather than the Windows
# share's uniform view of them.
#
# Every check is a function over (expected, observed) so that the same code that accepts the real tree can be shown to refuse a
# defective one. Four defects are exercised against a small scratch tree that this module creates itself: a missing file, a
# changed hash, a symlink in place of an ordinary file, and a wrong mode. A checker that has only ever accepted is not known to
# be able to refuse.

import hashlib
import json
import os
import stat
import sys


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, 'rb') as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def walk(root):
    """Every entry under root, as ordinary files and directories, with the identity the filesystem reports."""
    files = []
    directories = []
    refusals = []
    for base, dirnames, filenames in os.walk(root, followlinks=False):
        dirnames.sort()
        for name in sorted(dirnames):
            path = os.path.join(base, name)
            info = os.lstat(path)
            rel = os.path.relpath(path, root).replace(os.sep, '/')
            if stat.S_ISLNK(info.st_mode):
                refusals.append({'logical': rel, 'problem': 'a symlink where a directory is required'})
                continue
            directories.append({'logical': rel, 'mode': '%04o' % stat.S_IMODE(info.st_mode)})
        for name in sorted(filenames):
            path = os.path.join(base, name)
            info = os.lstat(path)
            rel = os.path.relpath(path, root).replace(os.sep, '/')
            if stat.S_ISLNK(info.st_mode):
                refusals.append({'logical': rel, 'problem': 'a symlink where an ordinary file is required'})
                continue
            if not stat.S_ISREG(info.st_mode):
                refusals.append({'logical': rel, 'problem': 'not a regular file'})
                continue
            files.append({'logical': rel, 'bytes': info.st_size, 'sha256': sha256_of(path),
                          'mode': '%04o' % stat.S_IMODE(info.st_mode), 'nlink': info.st_nlink,
                          'ino': info.st_ino,
                          'realpath_inside': os.path.realpath(path) == os.path.join(os.path.realpath(root), rel)})
    return files, directories, refusals


def compare(expected, observed):
    """The identity obligations, as one function so a negative can exercise exactly what the real tree is judged by."""
    problems = []
    by_logical = {}
    for row in observed:
        by_logical[row['logical']] = row
    for logical, want in expected.items():
        got = by_logical.get(logical)
        if got is None:
            problems.append({'logical': logical, 'why': 'declared but not present'})
            continue
        if got['bytes'] != want['bytes']:
            problems.append({'logical': logical, 'why': 'byte count', 'expected': want['bytes'], 'observed': got['bytes']})
        if got['sha256'] != want['sha256']:
            problems.append({'logical': logical, 'why': 'hash', 'expected': want['sha256'], 'observed': got['sha256']})
        if got['mode'] != want['mode']:
            problems.append({'logical': logical, 'why': 'mode', 'expected': want['mode'], 'observed': got['mode']})
        if got['nlink'] != 1:
            problems.append({'logical': logical, 'why': 'link count', 'observed': got['nlink']})
        if not got['realpath_inside']:
            problems.append({'logical': logical, 'why': 'resolves outside the destination'})
    for logical in by_logical:
        if logical not in expected:
            problems.append({'logical': logical, 'why': 'present but not declared'})
    inodes = [row['ino'] for row in observed]
    if len(set(inodes)) != len(inodes):
        problems.append({'why': 'two entries share an inode, so one is a hard link of the other'})
    return problems


def negatives(root, expected):
    """Build a small tree with four defects and require that compare() refuses each one."""
    require(not os.path.lexists(root), 'the negative scratch directory must be fresh: %r' % root)
    os.makedirs(root)
    plain = {'logical': 'a.bin', 'bytes': 4, 'sha256': hashlib.sha256(b'good').hexdigest(), 'mode': '0644'}
    with open(os.path.join(root, 'a.bin'), 'wb') as handle:
        handle.write(b'good')
    os.chmod(os.path.join(root, 'a.bin'), 0o644)
    os.symlink('a.bin', os.path.join(root, 'b.bin'))
    with open(os.path.join(root, 'c.bin'), 'wb') as handle:
        handle.write(b'good')
    os.chmod(os.path.join(root, 'c.bin'), 0o600)
    with open(os.path.join(root, 'd.bin'), 'wb') as handle:
        handle.write(b'bad!')
    os.chmod(os.path.join(root, 'd.bin'), 0o644)

    files, directories, refusals = walk(root)
    observed = {row['logical']: row for row in files}
    declared = {
        'a.bin': plain,
        'b.bin': {'logical': 'b.bin', 'bytes': 4, 'sha256': plain['sha256'], 'mode': '0644'},
        'c.bin': {'logical': 'c.bin', 'bytes': 4, 'sha256': plain['sha256'], 'mode': '0644'},
        'd.bin': {'logical': 'd.bin', 'bytes': 4, 'sha256': plain['sha256'], 'mode': '0644'},
        'e.bin': {'logical': 'e.bin', 'bytes': 1, 'sha256': plain['sha256'], 'mode': '0644'},
    }
    results = []
    # The control case: what the walker actually found, judged against a declaration that matches it, must be accepted. Without
    # this the four refusals below could all be produced by a comparator that refuses everything.
    control = compare({'a.bin': plain}, [observed['a.bin']])
    results.append({'name': 'control-accepted', 'refused': len(control) > 0, 'problems': control[:2],
                    'expected_refusal': False})
    # 1. a symlink where an ordinary file is required: the walker refuses to produce a row for it at all.
    results.append({'name': 'symlink-refused', 'refused': any('symlink' in r['problem'] for r in refusals),
                    'refusals': refusals, 'expected_refusal': True})
    # 2. a missing file.
    missing = compare(declared, [row for row in files])
    results.append({'name': 'missing-refused',
                    'refused': any(p['logical'] == 'e.bin' and p['why'] == 'declared but not present' for p in missing),
                    'problems': [p for p in missing if p['logical'] == 'e.bin'], 'expected_refusal': True})
    # 3. a changed hash.
    changed = compare({'d.bin': declared['d.bin']}, [observed['d.bin']])
    results.append({'name': 'hash-refused', 'refused': any(p['why'] == 'hash' for p in changed),
                    'problems': changed, 'expected_refusal': True})
    # 4. a wrong mode.
    wrong_mode = compare({'c.bin': declared['c.bin']}, [observed['c.bin']])
    results.append({'name': 'mode-refused', 'refused': any(p['why'] == 'mode' for p in wrong_mode),
                    'problems': wrong_mode, 'expected_refusal': True})
    ok = all((r['refused'] == r['expected_refusal']) for r in results)
    return results, ok, directories


def main(control_path):
    with open(control_path, 'r', encoding='utf-8') as handle:
        control = json.load(handle)
    require(control['task'] == 'GTOS-DSH-142', 'task')
    stage = control['stage']
    require(isinstance(stage, str) and stage, 'stage')
    runtime = control['runtime']
    inventory = control['inventory']
    require(runtime.startswith('/var/tmp/'), 'runtime destination must be under /var/tmp')
    require(os.path.isdir(runtime) and not os.path.islink(runtime), 'the runtime destination must be a real directory')

    with open(inventory['path'], 'rb') as handle:
        raw = handle.read()
    require(len(raw) == inventory['bytes'], 'inventory size')
    require(hashlib.sha256(raw).hexdigest() == inventory['sha256'], 'inventory pin')
    declared = json.loads(raw)
    expected = {}
    for row in declared['files']:
        expected[row['logical']] = {'logical': row['logical'], 'bytes': row['bytes'],
                                    'sha256': row['sha256'], 'mode': row['mode_octal']}

    files, directories, refusals = walk(runtime)
    problems = compare(expected, files)
    problems.extend(refusals)

    executable = sorted(row['logical'] for row in files if row['mode'] == '0755')
    declared_executable = sorted(['root/usr/bin/qemu-system-i386',
                                  'root/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2'])
    # A directory's execute bit means traversable, not runnable: every directory in a readable tree has it. What the packet
    # must not have is a program outside the declared pair, so the check is on the executable *files*, and the directory modes
    # are recorded so a surprising one is visible rather than judged by a rule that would refuse every usable tree.
    unusual_dirs = [d for d in directories if d['mode'] != '0755']

    neg, neg_ok, _ = negatives(control['negative_root'], expected)

    report = {
        'task': control['task'], 'stage': stage, 'kind': 'runtime-identity-reader',
        'runtime': runtime,
        'corrections': [
            'the first version of this reader treated a directory execute bit as a program grant and refused the whole tree. '
            'A directory execute bit is what makes a path traversable, and every usable tree has it; the obligation is that '
            'no program outside the declared pair is executable, and that is checked on the files.',
        ],
        'counted': {'files': len(files), 'bytes': sum(r['bytes'] for r in files),
                    'directories': len(directories),
                    'readonly_0644': len([r for r in files if r['mode'] == '0644']),
                    'executable_0755': len(executable)},
        'declared': {'files': declared['files_count'], 'bytes': declared['bytes']},
        'executable_files': executable,
        'executable_set_matches': executable == declared_executable,
        'directory_modes': sorted(set(d['mode'] for d in directories)),
        'directories_all_traversable': all(int(d['mode'], 8) & 0o111 for d in directories),
        'directories_with_an_unusual_mode': unusual_dirs,
        'problems': problems,
        'negatives': neg,
        'negatives_ok': neg_ok,
        'rows': files,
        'ok': (len(problems) == 0 and neg_ok and executable == declared_executable
               and len(unusual_dirs) == 0
               and len(files) == declared['files_count']
               and sum(r['bytes'] for r in files) == declared['bytes']),
    }
    with open(control['report_path'], 'w', encoding='utf-8', newline='\n') as handle:
        json.dump(report, handle, indent=1, sort_keys=True)
        handle.write('\n')
    print(json.dumps({'ok': report['ok'], 'files': len(files), 'bytes': report['counted']['bytes'],
                      'negatives_ok': neg_ok, 'problems': len(problems)}))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv[1]))
    except Exception as error:  # noqa: BLE001 - a refusal must reach the guard as a non-zero exit
        sys.stderr.write('%s: %s\n' % (type(error).__name__, error))
        sys.exit(1)
