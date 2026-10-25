#!/usr/bin/env python3
# GTOS-DSH-110 native metadata helper.
#
# Read-only filesystem measurement inside the guard. It stats, realpaths and hashes the named files, verifies the
# published ext4 runtime against the fixed inventory, and reports the filesystem facts it actually reads.
#
# It spawns nothing, evaluates no source, and builds nothing.

import hashlib
import json
import os
import stat
import sys

MAX_FILES = 4096


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def filesystem_of(path):
    """The real filesystem facts, read from /proc/mounts rather than guessed."""
    real = os.path.realpath(path)
    row = {'path': path, 'realpath': real, 'is_symlink': stat.S_ISLNK(os.lstat(path).st_mode)}
    mounts = []
    try:
        with open('/proc/mounts', 'r', encoding='utf-8') as handle:
            mounts = [line.split() for line in handle if line.strip()]
    except OSError:
        mounts = []
    best = None
    for parts in mounts:
        if len(parts) < 3:
            continue
        point, fstype = parts[1], parts[2]
        if real == point or real.startswith(point.rstrip('/') + '/'):
            if best is None or len(point) > len(best[0]):
                best = (point, fstype)
    row['mount_point'] = best[0] if best else None
    row['fstype'] = best[1] if best else None
    row['is_ext4'] = row['fstype'] == 'ext4'
    row['not_a_symlink'] = not row['is_symlink']
    row['ok'] = row['is_ext4'] and row['not_a_symlink'] and row['realpath'] == path
    return row


def measure_tool(entry):
    path = entry['path']
    row = {'label': entry['label'], 'path': path}
    try:
        st = os.lstat(path)
    except OSError as error:
        row['state'] = 'absent_ENOENT' if getattr(error, 'errno', None) == 2 else 'inaccessible'
        row['ok'] = False
        return row
    row['state'] = 'present'
    row['is_ordinary_file'] = stat.S_ISREG(st.st_mode)
    row['nlink'] = st.st_nlink
    row['size'] = st.st_size
    row['sha256'] = digest(path) if row['is_ordinary_file'] else None
    row['matches_expected'] = row['size'] == entry['bytes'] and row['sha256'] == entry['sha256']
    row['ok'] = row['is_ordinary_file'] and row['matches_expected']
    return row


def measure_runtime(root, files):
    rows = []
    problems = []
    inodes = set()
    total = 0
    for item in files:
        target = os.path.join(root, *item['logical'].split('/'))
        row = {'logical': item['logical'], 'path': target}
        try:
            st = os.lstat(target)
        except OSError:
            row['state'] = 'absent'
            problems.append(item['logical'])
            rows.append(row)
            continue
        row['state'] = 'present'
        row['is_ordinary_file'] = stat.S_ISREG(st.st_mode)
        row['is_symlink'] = stat.S_ISLNK(st.st_mode)
        row['nlink'] = st.st_nlink
        row['size'] = st.st_size
        row['mode_octal'] = oct(stat.S_IMODE(st.st_mode))[2:].zfill(4)
        row['sha256'] = digest(target) if row['is_ordinary_file'] else None
        row['bytes_match'] = row['size'] == item['bytes']
        row['hash_match'] = row['sha256'] == item['sha256']
        row['mode_match'] = row['mode_octal'] == item['mode_octal']
        row['nlink_ok'] = row['nlink'] == 1
        row['inode'] = st.st_ino
        row['inode_unique'] = st.st_ino not in inodes
        row['realpath_in_runtime'] = os.path.realpath(target).startswith(root + os.sep)
        if row['inode_unique']:
            inodes.add(st.st_ino)
        else:
            problems.append(item['logical'] + ' (duplicate inode)')
        row['ok'] = (row['is_ordinary_file'] and not row['is_symlink'] and row['bytes_match']
                     and row['hash_match'] and row['mode_match'] and row['nlink_ok']
                     and row['inode_unique'] and row['realpath_in_runtime'])
        if not row['ok']:
            problems.append(item['logical'])
        total += row['size']
        rows.append(row)
    return rows, problems, total


def main(control_path):
    with open(control_path, 'r', encoding='utf-8') as handle:
        control = json.load(handle)
    require(control['task'] == 'GTOS-DSH-110', 'task')

    checks = []

    def check(name, ok, detail=None):
        checks.append({'check': name, 'ok': bool(ok), 'detail': detail})

    root = control['runtime_root']
    files = control['inventory_files']
    require(isinstance(files, list) and 0 < len(files) <= MAX_FILES, 'inventory bound')

    fs_row = filesystem_of(root)
    check('the published runtime is on ext4', fs_row['is_ext4'], fs_row['fstype'])
    check('the runtime realpath is itself and not a symlink',
          fs_row['realpath'] == root and fs_row['not_a_symlink'], fs_row['realpath'])

    rows, problems, total = measure_runtime(root, files)
    check('the runtime holds exactly %d files' % len(files), len(rows) == len(files), len(rows))
    check('the runtime totals %d bytes' % sum(f['bytes'] for f in files),
          total == sum(f['bytes'] for f in files), total)
    check('every runtime file matches its inventory bytes, hash and mode', not problems,
          problems[:12])
    modes = {}
    for row in rows:
        if row.get('mode_octal'):
            modes[row['mode_octal']] = modes.get(row['mode_octal'], 0) + 1
    check('exactly two runtime files are 0755', modes.get('0755') == 2, modes)
    check('every other runtime file is 0644', modes.get('0644') == len(files) - 2, modes)
    check('every runtime file is an ordinary single-linked file',
          all(r.get('nlink') == 1 for r in rows if r.get('state') == 'present'), True)
    check('every runtime inode is unique',
          all(r.get('inode_unique') for r in rows if r.get('state') == 'present'), True)
    check('every runtime realpath stays inside the runtime root',
          all(r.get('realpath_in_runtime') for r in rows if r.get('state') == 'present'), True)

    tools = [measure_tool(t) for t in control['tools']]
    check('every named tool is an ordinary file at its pin', all(t['ok'] for t in tools),
          [t['label'] for t in tools if not t['ok']])

    report = {
        'task': control['task'], 'kind': 'native_metadata', 'read_only': True,
        'spawned_anything': False, 'filesystem': fs_row,
        'runtime': {'root': root, 'files': len(rows), 'bytes': total,
                    'rows': rows, 'problems': problems},
        'tools': tools,
        'checks': checks, 'checks_total': len(checks),
        'checks_failed': len([c for c in checks if not c['ok']]),
        'ok': all(c['ok'] for c in checks),
    }
    with open(control['report_path'], 'w', encoding='utf-8', newline='\n') as handle:
        json.dump(report, handle, indent=1, sort_keys=True)
        handle.write('\n')
    print(json.dumps({'ok': report['ok'], 'checks': len(checks),
                      'failed': report['checks_failed']}))
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv[1]))
    except Exception as error:  # noqa: BLE001
        sys.stderr.write('%s: %s\n' % (type(error).__name__, error))
        sys.exit(1)
