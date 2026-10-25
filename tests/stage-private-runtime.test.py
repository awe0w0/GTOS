import copy
import hashlib
import importlib.util
import json
import os
import stat
import sys

with open(sys.argv[1], 'r', encoding='utf-8') as handle:
    control = json.load(handle)
with open(control['module']['path'], 'rb') as handle:
    module_bytes = handle.read()
assert len(module_bytes) == control['module']['bytes']
assert hashlib.sha256(module_bytes).hexdigest() == control['module']['sha256']
spec = importlib.util.spec_from_file_location('tested_private_runtime', control['module']['path'])
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
root = control['fixture_root']
os.makedirs(root, exist_ok=False)
assertions = 0


def check(value, label):
    global assertions
    assertions += 1
    if not value:
        raise AssertionError(label)


def reject(action, label):
    try:
        action()
    except (ValueError, OSError):
        check(True, label)
    else:
        check(False, label)


source = os.path.join(root, 'source')
os.mkdir(source)
names = ['root/usr/bin/qemu-system-i386', 'root/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2', 'firmware/bios-256k.bin']
rows = []
for index, name in enumerate(names):
    target = os.path.join(source, name)
    os.makedirs(os.path.dirname(target), exist_ok=True)
    data = ('TEST_ONLY file ' + str(index)).encode('ascii')
    with open(target, 'xb') as handle:
        handle.write(data)
    rows.append({'logical': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                 'mode_octal': '0644' if name.startswith('firmware/') else '0755'})
inventory = {'files': rows}
counter = 0


def invoke(value):
    global counter
    counter += 1
    work = os.path.join(root, 'run-' + str(counter))
    os.mkdir(work)
    return module.stage_runtime(value, source, work, 'rtstage-01'), work


result, work = invoke(inventory)
check(result['ok'] and result['file_count'] == 3 and result['runtime_executed'] is False, 'actual production copy function')
for row in rows:
    target = os.path.join(work, 'runtime', row['logical'])
    with open(target, 'rb') as handle:
        check(hashlib.sha256(handle.read()).hexdigest() == row['sha256'], 'destination bytes')
    check(stat.S_IMODE(os.stat(target).st_mode) == int(row['mode_octal'], 8), 'real POSIX destination mode')
    check(os.stat(target).st_nlink == 1, 'independent destination')
reject(lambda: module.stage_runtime(inventory, source, work, 'rtstage-02'), 'published destination cannot be overwritten')
for name in ['../outside', '/absolute', 'a\\b', 'a:b', 'a//b', 'a/./b']:
    bad = copy.deepcopy(inventory)
    bad['files'][0]['logical'] = name
    reject(lambda: invoke(bad), 'bad relative name')
bad = copy.deepcopy(inventory)
bad['files'].append(copy.deepcopy(bad['files'][0]))
reject(lambda: invoke(bad), 'duplicate path')
bad = copy.deepcopy(inventory)
bad['files'][0]['sha256'] = '0' * 64
reject(lambda: invoke(bad), 'source pin mismatch')
bad = copy.deepcopy(inventory)
bad['files'][2]['mode_octal'] = '0755'
reject(lambda: invoke(bad), 'data cannot acquire EXEC')
bad = copy.deepcopy(inventory)
bad['files'][0]['mode_octal'] = '0644'
reject(lambda: invoke(bad), 'binary must get intended private mode')
bad = copy.deepcopy(inventory)
bad['files'][0]['bytes'] = 257 * 1024 * 1024
reject(lambda: invoke(bad), 'bounded total size')
reject(lambda: invoke({'files': rows * 100}), 'bounded count')
outside = os.path.join(root, 'outside.bin')
with open(outside, 'xb') as handle:
    handle.write(b'outside')
os.symlink(outside, os.path.join(source, 'escape'))
bad = {'files': [{'logical': 'escape', 'bytes': 7, 'sha256': hashlib.sha256(b'outside').hexdigest(), 'mode_octal': '0644'}]}
reject(lambda: invoke(bad), 'source symlink escape')
alias = os.path.join(source, 'alias')
os.symlink('firmware/bios-256k.bin', alias)
alias_row = dict(rows[2], logical='alias')
alias_result, alias_work = invoke({'files': [alias_row]})
check(alias_result['ok'] and not os.path.islink(os.path.join(alias_work, 'runtime', 'alias')), 'internal alias materializes ordinary file')
for unused in range(64):
    again, unused_work = invoke(inventory)
    if not again['ok'] or again['file_count'] != 3:
        raise AssertionError('stability cycle')
check(True, '64 fresh copy and publish cycles')
check(assertions >= 26, 'coverage count')
report = {'ok': True, 'assertions': assertions, 'cycles': 64, 'module': control['module'],
          'fixtures_are_test_only': True, 'runtime_or_guest_executed': False}
with open(control['result_path'], 'x', encoding='utf-8') as handle:
    json.dump(report, handle, indent=2)
    handle.write('\n')
print(json.dumps(report))
