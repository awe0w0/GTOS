import hashlib
import json
import os
import stat
import sys


def require(ok, message):
    if not ok:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def stage_runtime(inventory, source_root, work_root, stage):
    require(os.path.realpath(work_root) == os.path.abspath(work_root), 'canonical destination owner')
    source_root = os.path.realpath(source_root)
    work_root = os.path.realpath(work_root)
    require(stage.startswith('rtstage-') and len(stage) == 10 and stage[-2:].isdigit(), 'stage identity')
    rows = inventory['files']
    require(isinstance(rows, list) and 0 < len(rows) <= 256, 'file count bound')
    require(sum(r['bytes'] for r in rows) <= 256 * 1024 * 1024, 'byte bound')
    require(len({r['logical'] for r in rows}) == len(rows), 'unique paths')
    for row in rows:
        name = row['logical']
        require(isinstance(name, str) and name and '\\' not in name and ':' not in name
                and not name.startswith('/') and all(x not in ('', '.', '..') for x in name.split('/')), 'relative path')
        require(isinstance(row['bytes'], int) and row['bytes'] >= 0, 'byte count')
        require(row['mode_octal'] in ('0644', '0755'), 'fixed mode')
        code = name in ('root/usr/bin/qemu-system-i386', 'root/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2')
        require((row['mode_octal'] == '0755') == code, 'only loader and emulator executable')
    destination = os.path.join(work_root, 'runtime')
    require(not os.path.lexists(destination), 'published destination already exists')
    scratch = os.path.join(work_root, 'staging', stage)
    os.makedirs(scratch, exist_ok=False)
    tree = os.path.join(scratch, 'runtime')
    os.mkdir(tree)
    results = []
    inodes = set()
    for row in rows:
        source = os.path.realpath(os.path.join(source_root, row['logical']))
        require(os.path.commonpath([source, source_root]) == source_root and source != source_root, 'source alias escapes root')
        require(stat.S_ISREG(os.stat(source).st_mode), 'source is regular')
        with open(source, 'rb') as handle:
            data = handle.read(row['bytes'] + 1)
        require(len(data) == row['bytes'] and digest(data) == row['sha256'], 'source pin')
        target = os.path.join(tree, *row['logical'].split('/'))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        with open(target, 'xb') as handle:
            handle.write(data)
        os.chmod(target, int(row['mode_octal'], 8))
        info = os.lstat(target)
        require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1, 'independent ordinary file')
        require(info.st_ino not in inodes, 'unique inode')
        inodes.add(info.st_ino)
        require(stat.S_IMODE(info.st_mode) == int(row['mode_octal'], 8), 'destination mode')
        with open(target, 'rb') as handle:
            require(digest(handle.read()) == row['sha256'], 'destination rehash')
        results.append({'logical': row['logical'], 'bytes': info.st_size, 'sha256': row['sha256'],
                        'mode': oct(stat.S_IMODE(info.st_mode)), 'nlink': info.st_nlink})
    require(not os.path.lexists(destination), 'destination changed before publish')
    os.rename(tree, destination)
    return {'ok': True, 'stage': stage, 'files': results, 'file_count': len(results),
            'bytes': sum(r['bytes'] for r in results), 'destination': destination,
            'published_once': True, 'runtime_executed': False}


def main(control_path):
    with open(control_path, 'r', encoding='utf-8') as handle:
        control = json.load(handle)
    require(control['task'] == 'GTOS-DSH-108', 'task')
    with open(control['inventory']['path'], 'rb') as handle:
        data = handle.read()
    require(len(data) == control['inventory']['bytes'] and digest(data) == control['inventory']['sha256'], 'inventory pin')
    require(control['source_root'] == '/mnt/f/GTOS-Chromium/toolchains/gtos-qemu8-native-files-a', 'fixed source')
    require(control['work_root'] == '/var/tmp/gtos-dsh-108-e759-e99-inputs', 'fixed destination owner')
    result = stage_runtime(json.loads(data), control['source_root'], control['work_root'], control['stage'])
    with open(control['result_path'], 'x', encoding='utf-8') as handle:
        json.dump(result, handle, indent=2)
        handle.write('\n')
    print(json.dumps({'ok': True, 'files': result['file_count'], 'bytes': result['bytes']}))


if __name__ == '__main__':
    main(sys.argv[1])
