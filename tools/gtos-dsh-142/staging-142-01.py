#!/usr/bin/env python3
# GTOS-DSH-142 source view staging module.
#
# This is the single production staging implementation. It replaces the 109 helper, whose control carried a key
# literally named `require` that shadowed the assertion function, and whose control handed WSL `F:/` paths that Python
# inside WSL cannot open.
#
# Two rules make those failures impossible here:
#
#   1. The control is read through an explicit namespace (`control["request"]`), and every key is validated against a
#      reserved-name list BEFORE anything is bound. A colliding key is a refusal, not a renamed fixture.
#   2. Every file reference in the request is required to be an absolute Linux path. A Windows drive letter anywhere in
#      the request is a refusal, so a partially converted control cannot reach the copy loop.

import hashlib
import json
import os
import stat
import sys

# Names the module binds itself. A control key that takes one of these would shadow it.
RESERVED = frozenset([
    'require', 'main', 'digest', 'hashlib', 'json', 'os', 'stat', 'sys', 'open', 'control',
])

MAX_FILES = 4096
MAX_BYTES = 64 * 1024 * 1024


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def assert_no_reserved_keys(value, trail='control'):
    """Refuse any control key that would shadow a name this module binds."""
    if isinstance(value, dict):
        for key, item in value.items():
            require(key not in RESERVED, 'reserved control key at %s.%s' % (trail, key))
            assert_no_reserved_keys(item, '%s.%s' % (trail, key))
    elif isinstance(value, list):
        for index, item in enumerate(value):
            assert_no_reserved_keys(item, '%s[%d]' % (trail, index))


def assert_linux_path(value, label):
    require(isinstance(value, str) and bool(value), '%s must be a non-empty string' % label)
    require(value.startswith('/'), '%s must be an absolute Linux path, got %r' % (label, value))
    require('\\' not in value, '%s must not carry a backslash: %r' % (label, value))
    require('\0' not in value, '%s must not carry a NUL' % label)
    require(os.path.normpath(value) == value, '%s must be canonical: %r' % (label, value))
    return value


def stage(request, work_root):
    """Copy every declared file into a fresh wave directory owned by this stage."""
    require(isinstance(request, dict), 'request must be an object')
    # The request is external input, so the reserved-key rule is enforced here as well as at the control boundary.
    # A caller that reaches stage() directly must not be able to smuggle a shadowing key past main().
    assert_no_reserved_keys(request, 'request')
    wave = request['wave']
    require(isinstance(wave, str) and len(wave) == 7 and wave.startswith('wave-')
            and wave[5:].isdigit(), 'wave identity')
    files = request['files']
    require(isinstance(files, list) and 0 < len(files) <= MAX_FILES, 'file count bound')
    require(sum(f['bytes'] for f in files) <= MAX_BYTES, 'byte bound')

    logicals = set()
    for entry in files:
        logical = entry['logical']
        require(isinstance(logical, str) and bool(logical), 'logical identity')
        require(not logical.startswith('/'), 'logical identity must be relative: %r' % logical)
        require('\\' not in logical and ':' not in logical, 'logical identity must be POSIX: %r' % logical)
        parts = logical.split('/')
        require(all(p not in ('', '.', '..') for p in parts), 'logical identity must be safe: %r' % logical)
        require(logical not in logicals, 'duplicate logical identity: %r' % logical)
        logicals.add(logical)
        require(isinstance(entry['bytes'], int) and entry['bytes'] >= 0, 'byte count for %r' % logical)
        require(isinstance(entry['sha256'], str) and len(entry['sha256']) == 64,
                'pin for %r' % logical)
        assert_linux_path(entry['source'], 'source for %r' % logical)

    require(work_root.startswith('/var/tmp/'), 'work root must be under /var/tmp')
    require(os.path.realpath(work_root) == work_root, 'work root must be canonical')
    destination = os.path.join(work_root, wave)
    # The wave belongs to this stage. An existing one refuses and is left exactly as it was.
    require(not os.path.lexists(destination),
            'wave %r already exists; a wave is owned by one stage and is never reused or cleared' % wave)

    rows = []
    inodes = set()
    for entry in files:
        logical = entry['logical']
        source = entry['source']
        require(os.path.realpath(source) == source, 'source must be canonical: %r' % source)
        require(stat.S_ISREG(os.stat(source).st_mode), 'source must be a regular file: %r' % source)
        with open(source, 'rb') as handle:
            data = handle.read(entry['bytes'] + 1)
        require(len(data) == entry['bytes'], 'source size mismatch for %r' % logical)
        require(hashlib.sha256(data).hexdigest() == entry['sha256'],
                'source pin mismatch for %r' % logical)
        target = os.path.join(destination, *logical.split('/'))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, 'xb') as handle:
            handle.write(data)
        info = os.lstat(target)
        require(stat.S_ISREG(info.st_mode) and not stat.S_ISLNK(info.st_mode),
                'destination must be an ordinary file: %r' % logical)
        require(info.st_nlink == 1, 'destination must have nlink 1: %r' % logical)
        require(info.st_ino not in inodes, 'destination inode must be unique: %r' % logical)
        inodes.add(info.st_ino)
        require(info.st_size == entry['bytes'], 'destination size mismatch for %r' % logical)
        require(digest(target) == entry['sha256'], 'destination rehash mismatch for %r' % logical)
        rows.append({'logical': logical, 'bytes': info.st_size, 'sha256': entry['sha256'],
                     'inode': info.st_ino, 'nlink': info.st_nlink})

    return {'ok': True, 'wave': wave, 'destination': destination,
            'files': len(rows), 'bytes': sum(r['bytes'] for r in rows),
            'rows': rows, 'executed_anything': False, 'compiled_anything': False}


def stage_runtime(inventory, work_root, stage_id, source_root):
    """Copy the fixed original bundle into a fresh private ext4 tree, verify every byte, publish with one rename."""
    require(work_root.startswith('/var/tmp/'), 'work root must be under /var/tmp')
    require(os.path.realpath(work_root) == work_root, 'work root must be canonical')
    files = inventory['files']
    require(isinstance(files, list) and 0 < len(files) <= 4096, 'file count bound')
    require(sum(f['bytes'] for f in files) <= 256 * 1024 * 1024, 'byte bound')
    destination = os.path.join(work_root, 'runtime')
    precreated_empty = False
    if os.path.lexists(destination):
        # A published runtime is never overwritten or rebuilt. The single exception is an empty directory, which this
        # packet's own setup created as a declared output parent; that is recorded, not hidden.
        require(os.path.isdir(destination) and not os.path.islink(destination),
                'a published runtime is never overwritten or rebuilt')
        require(not os.listdir(destination),
                'a published runtime is never overwritten or rebuilt')
        precreated_empty = True
    scratch = os.path.join(work_root, 'staging', stage_id)
    require(not os.path.lexists(scratch), 'fresh scratch directory')
    os.makedirs(scratch)
    tree = os.path.join(scratch, 'runtime')
    os.mkdir(tree)
    rows = []
    inodes = set()
    for item in files:
        logical = item['logical']
        require(isinstance(logical, str) and bool(logical) and not logical.startswith('/'),
                'logical identity for a runtime entry')
        require('\\' not in logical and ':' not in logical, 'runtime logical identity is POSIX')
        parts = logical.split('/')
        require(all(p not in ('', '.', '..') for p in parts), 'runtime logical identity is safe')
        require(item['mode_octal'] in ('0644', '0755'), 'fixed mode for %r' % logical)
        executable = logical in ('root/usr/bin/qemu-system-i386',
                                 'root/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2')
        require((item['mode_octal'] == '0755') == executable,
                'only the bundled loader and the real QEMU are executable: %r' % logical)
        source = item['source']
        assert_linux_path(source, 'runtime source')
        resolved = os.path.realpath(source)
        require(os.path.commonpath([resolved, source_root]) == source_root and resolved != source_root,
                'runtime source alias resolves inside the fixed source root: %r' % logical)
        require(stat.S_ISREG(os.stat(source).st_mode), 'runtime source resolves to a regular file')
        with open(source, 'rb') as handle:
            data = handle.read(item['bytes'] + 1)
        require(len(data) == item['bytes'], 'runtime source size for %r' % logical)
        require(hashlib.sha256(data).hexdigest() == item['sha256'], 'runtime source pin for %r' % logical)
        target = os.path.join(tree, *logical.split('/'))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, 'xb') as handle:
            handle.write(data)
        os.chmod(target, int(item['mode_octal'], 8))
        info = os.lstat(target)
        require(stat.S_ISREG(info.st_mode) and not stat.S_ISLNK(info.st_mode), 'ordinary file: %r' % logical)
        require(info.st_nlink == 1, 'nlink 1: %r' % logical)
        require(info.st_ino not in inodes, 'unique inode: %r' % logical)
        inodes.add(info.st_ino)
        require(stat.S_IMODE(info.st_mode) == int(item['mode_octal'], 8), 'mode: %r' % logical)
        require(digest(target) == item['sha256'], 'rehash: %r' % logical)
        rows.append({'logical': logical, 'bytes': info.st_size, 'sha256': item['sha256'],
                     'mode': oct(stat.S_IMODE(info.st_mode)), 'nlink': info.st_nlink})
    if precreated_empty:
        os.rmdir(destination)
    require(not os.path.lexists(destination), 'destination appeared before publish')
    os.rename(tree, destination)
    return {'ok': True, 'stage': stage_id, 'destination': destination,
            'files': len(rows), 'bytes': sum(r['bytes'] for r in rows),
            'rows': rows, 'published_once': True, 'runtime_executed': False,
            'destination_precreated_empty_by_setup': precreated_empty}


def main(control_path):
    with open(control_path, 'r', encoding='utf-8') as handle:
        control = json.load(handle)
    require(isinstance(control, dict), 'control must be an object')
    # Validate the whole control before binding anything, so a colliding key cannot shadow a function.
    assert_no_reserved_keys(control)
    require(control['task'] == 'GTOS-DSH-142', 'task')
    report_path = assert_linux_path(control['report_path'], 'report path')
    work_root = assert_linux_path(control['source_root'], 'source root')
    if control.get('mode') == 'runtime':
        inventory_path = assert_linux_path(control['inventory']['path'], 'inventory path')
        with open(inventory_path, 'rb') as handle:
            raw = handle.read()
        require(len(raw) == control['inventory']['bytes'], 'inventory size')
        require(hashlib.sha256(raw).hexdigest() == control['inventory']['sha256'], 'inventory pin')
        report = stage_runtime(json.loads(raw), work_root, control['stage'],
                               assert_linux_path(control['original_source_root'], 'original source root'))
    else:
        request = control['request']
        require(isinstance(request, dict), 'request namespace')
        report = stage(request, work_root)
    report['task'] = control['task']
    report['kind'] = 'runtime' if control.get('mode') == 'runtime' else 'source_view'
    with open(report_path, 'w', encoding='utf-8', newline='\n') as handle:
        json.dump(report, handle, indent=1, sort_keys=True)
        handle.write('\n')
    print(json.dumps({'ok': True, 'files': report['files'], 'bytes': report['bytes']}))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv[1]))
    except Exception as error:  # noqa: BLE001 - a refusal must reach the guard as a non-zero exit
        sys.stderr.write('%s: %s\n' % (type(error).__name__, error))
        sys.exit(1)
