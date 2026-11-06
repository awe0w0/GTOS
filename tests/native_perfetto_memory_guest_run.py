#!/usr/bin/env python3
"""Run the actual native page consumer and judge complete CPL3/Reap records."""
import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path


def judge(lines, code):
    groups = {'PMEM CASE': {}, 'PMEM REAP': {}}
    fields = {'PMEM CASE': {'mode', 'checks', 'base', 'cs', 'calls', 'exit', 'fault', 'error'},
              'PMEM REAP': {'mode', 'free', 'expected'}}
    problems = []
    for line in lines:
        for name in groups:
            if not line.startswith(name + ' '):
                continue
            values = {}
            for word in line[len(name) + 1:].split():
                match = re.fullmatch(r'([a-z]+)=([0-9A-F]{8})', word)
                if not match or match[1] in values:
                    problems.append('malformed ' + name)
                    break
                values[match[1]] = int(match[2], 16)
            if set(values) != fields[name] or values.get('mode') not in range(5):
                problems.append('incomplete ' + name)
                continue
            mode = values['mode']
            if mode in groups[name]:
                problems.append('duplicate ' + name)
            groups[name][mode] = values
    complete = all(set(group) == set(range(5)) for group in groups.values())
    if complete:
        cases, reaps = groups['PMEM CASE'], groups['PMEM REAP']
        baseline = reaps[0]['expected']
        for mode, value in cases.items():
            if value['cs'] != 0x23 or value['calls'] != [147, 3, 3, 2, 4][mode]:
                problems.append('context/call count ' + str(mode))
            if mode == 0:
                if any(value[name] for name in ['base', 'exit', 'fault', 'error']) or value['checks'] != 114:
                    problems.append('consumer comparisons')
            else:
                offset = -4096 if mode == 1 else 8192 if mode == 2 else 0
                if not 0x80000000 <= value['base'] < 0xBFFFC000 or value['fault'] != value['base'] + offset:
                    problems.append('fault address ' + str(mode))
                if value['exit'] != 0x8000000E or value['error'] & 7 != (4 if mode == 3 else 6) or value['checks']:
                    problems.append('fault policy ' + str(mode))
            if not baseline or reaps[mode]['free'] != baseline or reaps[mode]['expected'] != baseline:
                problems.append('reap accounting ' + str(mode))
    else:
        problems.append('missing case/reap records')
    required = {'PERFETTO MEMORY GUEST BOOT': 1, 'PERFETTO MEMORY GUEST PASS': 1,
                'PMEM PROFILE SSE2': 1,
                'PMEM CONSUMER BEGIN': 5, 'PMEM CONSUMER PASS': 1}
    for text, count in required.items():
        if lines.count(text) != count:
            problems.append('wrong line count ' + text)
    if code != 33 or any('FAIL' in line or 'UNEXPECTED' in line or 'ASSERT' in line for line in lines):
        problems.append('guest failure/exit')
    return {'ok': not problems, 'problems': problems, 'records': groups}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    manifest = args.build / 'manifest.json'
    manifest_bytes = manifest.read_bytes()
    judge_source_sha256 = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    record = json.loads(manifest_bytes)
    assert record['kernel_fp_policy'] == 'NativeFpSse2' and record['user_profile'] == 'GTOS IA32 SSE2, matching native Content'
    assert record['source_before_after_identical'] and len(record['modules']) == 12
    assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == digest for path, digest in record['inputs'].items())
    assert len({(entry['optimization'], entry['mode'], entry['negative']) for entry in record['modules']}) == 12
    entries = [record['kernel'], *record['modules']]

    def verify():
        for entry in entries:
            path = Path(entry['path'])
            assert args.build.resolve() in path.resolve().parents
            assert path.is_file() and not path.is_symlink() and not path.stat().st_mode & 0o222
            assert path.stat().st_size == entry['bytes'] and hashlib.sha256(path.read_bytes()).hexdigest() == entry['sha256']
        assert manifest.read_bytes() == manifest_bytes

    verify()
    results = []
    for optimization, memory, negative in [(0, '32M', False), (2, '64M', False), (2, '32M', True)]:
        name = 'O%d-%s%s' % (optimization, memory, '-negative' if negative else '')
        directory = args.out / name
        directory.mkdir()
        modules = []
        for mode in range(5):
            entry = next(value for value in record['modules'] if value['optimization'] == optimization
                         and value['mode'] == mode and value['negative'] == (negative and mode == 0))
            modules.append(entry['path'])
        command = [os.environ['LOADER'], '--library-path', os.environ['QEMU_LIB'], os.environ['QEMU'],
                   '-L', os.environ['QEMU_DATA'], '-machine', 'pc', '-accel', 'tcg,thread=single',
                   '-cpu', 'max', '-m', memory, '-smp', '1', '-kernel', record['kernel']['path'],
                   '-initrd', ','.join(modules), '-icount', 'shift=0,align=off,sleep=off',
                   '-nic', 'none', '-display', 'none', '-monitor', 'none', '-serial', 'none',
                   '-debugcon', 'file:' + str(directory / 'debugcon.log'),
                   '-device', 'isa-debug-exit,iobase=0xf4,iosize=4', '-no-reboot']
        with (directory / 'command.json').open('x') as handle:
            json.dump(command, handle, indent=2)
        with (directory / 'qemu.stderr').open('xb') as handle:
            result = subprocess.run(command, stdout=handle, stderr=subprocess.STDOUT, timeout=90)
        raw = (directory / 'debugcon.log').read_bytes()
        lines = raw.decode('ascii').splitlines()
        verdict = judge(lines, result.returncode)
        verdict.update({'exit': result.returncode, 'log_bytes': len(raw),
                        'log_sha256': hashlib.sha256(raw).hexdigest(), 'negative': negative})
        with (directory / 'verdict.json').open('x') as handle:
            json.dump(verdict, handle, indent=2)
        print(name, 'qemu=' + str(result.returncode), 'pass=' + str(verdict['ok']), '\n'.join(lines[-14:]), flush=True)
        if negative:
            assert result.returncode == 65 and not verdict['ok'] and 'PMEM CONSUMER FAIL' in lines
            assert 'PMEM FAILED consumer result' in lines and lines.count('PERFETTO MEMORY GUEST FAIL') == 1
        else:
            assert verdict['ok'], verdict['problems']
            # These mutate the real judge's input, never the captured raw log.
            mutations = [('exit', lines, 0), ('missing all', [], 33), ('duplicate pass', lines + ['PERFETTO MEMORY GUEST PASS'], 33)]
            for prefix in ['PMEM CASE', 'PMEM REAP']:
                for line in [value for value in lines if value.startswith(prefix + ' ')]:
                    mutations.append(('duplicate', lines + [line], 33))
                    for field in line.split()[2:]:
                        changed = line.replace(' ' + field, '', 1)
                        mutations.append(('missing field', [changed if value == line else value for value in lines], 33))
                        key, original = field.split('=')
                        replacement = '%s=%08X' % (key, int(original, 16) ^ 1)
                        changed = line.replace(field, replacement, 1)
                        mutations.append(('changed field', [changed if value == line else value for value in lines], 33))
            for description, changed, code in mutations:
                assert not judge(changed, code)['ok'], description
            verdict['rejected_judge_mutations'] = len(mutations)
        verify()
        results.append(verdict)
    assert hashlib.sha256(Path(__file__).read_bytes()).hexdigest() == judge_source_sha256
    with (args.out / 'status.json').open('x') as handle:
        json.dump({'native_paged_memory_guest_pass': True, 'content_c1_c2_c3': False,
                   'manifest_sha256': hashlib.sha256(manifest_bytes).hexdigest(), 'results': results,
                   'judge_source_sha256': judge_source_sha256,
                   'scope': 'actual CPL3 page backend lifecycle, guards, fault containment and Reap; no browser claim'}, handle, indent=2)
    print('PERFETTO_MEMORY_GUEST_RUN_PASS positive=2 rejected_wrong_consumer=1')


if __name__ == '__main__':
    main()
