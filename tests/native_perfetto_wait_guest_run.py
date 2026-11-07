#!/usr/bin/env python3
"""Qualify actual Perfetto wait calls in current-source CPL3 lifetimes."""
import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path


def judge(lines, code):
    groups = {'PWAIT CASE': {}, 'PWAIT REAP': {}}
    fields = {'PWAIT CASE': {'mode', 'checks', 'cs', 'calls', 'waits', 'sleeps', 'yields', 'exit', 'vector', 'handles', 'created', 'closed', 'signals'},
              'PWAIT REAP': {'mode', 'free', 'expected', 'handles', 'released'}}
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
            if set(values) != fields[name] or values.get('mode') not in range(3):
                problems.append('incomplete ' + name)
                continue
            mode = values['mode']
            if mode in groups[name]:
                problems.append('duplicate ' + name)
            groups[name][mode] = values
    if all(set(group) == set(range(3)) for group in groups.values()):
        baseline = groups['PWAIT REAP'][0]['expected']
        for mode, value in groups['PWAIT CASE'].items():
            expected = {'checks': 1 if mode else 14, 'cs': 0x23,
                        'waits': 1 if mode else 8, 'exit': 0x80000006 if mode else 0, 'vector': 6 if mode else 0,
                        'handles': 1 if mode else 0, 'created': 1 if mode else 2,
                        'closed': 0 if mode else 2, 'signals': 0 if mode else 3}
            if any(value[key] != wanted for key, wanted in expected.items()):
                problems.append('native wait case ' + str(mode))
            if mode:
                if value['calls'] != [0, 4, 3][mode] or value['sleeps'] or value['yields']:
                    problems.append('fault scheduling ' + str(mode))
            elif not (30 <= value['calls'] <= 100000000 and 3 <= value['sleeps'] <= 16 and 0 < value['yields'] <= 100000000):
                problems.append('native scheduling')
            reap = groups['PWAIT REAP'][mode]
            if not baseline or reap['free'] != baseline or reap['expected'] != baseline or reap['handles'] or reap['released'] != int(mode != 0):
                problems.append('Reap accounting ' + str(mode))
    else:
        problems.append('missing case/reap records')
    for text, count in {'PERFETTO WAIT GUEST BOOT': 1, 'PERFETTO WAIT GUEST PASS': 1,
                        'PWAIT CONSUMER BEGIN': 3, 'PWAIT CONSUMER PASS': 1, 'PWAIT REFUSAL': 2}.items():
        if lines.count(text) != count:
            problems.append('wrong line count ' + text)
    if code != 33 or any(any(word in line for word in ['FAIL', 'UNEXPECTED', 'ASSERT', 'TIMEOUT']) for line in lines):
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
    judge_sha = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    record = json.loads(manifest_bytes)
    assert record['component'] == 'wait' and record['kernel_fp_policy'] == 'NativeFpSse2'
    assert record['source_before_after_identical'] and len(record['modules']) == 8
    assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == digest for path, digest in record['inputs'].items())
    assert len({(entry['optimization'], entry['mode'], entry['negative']) for entry in record['modules']}) == 8

    def verify():
        for entry in [record['kernel'], *record['modules']]:
            path = Path(entry['path'])
            assert args.build.resolve() in path.resolve().parents
            assert path.is_file() and not path.is_symlink() and not path.stat().st_mode & 0o222
            assert path.stat().st_size == entry['bytes'] and hashlib.sha256(path.read_bytes()).hexdigest() == entry['sha256']
        assert manifest.read_bytes() == manifest_bytes

    verify()
    results = []
    for optimization, memory, negative in [(0, '32M', False), (2, '64M', False), (2, '32M', True)]:
        directory = args.out / ('O%d-%s%s' % (optimization, memory, '-negative' if negative else ''))
        directory.mkdir()
        modules = [next(value['path'] for value in record['modules'] if value['optimization'] == optimization
                        and value['mode'] == mode and value['negative'] == (negative and mode == 0)) for mode in range(3)]
        command = [os.environ['LOADER'], '--library-path', os.environ['QEMU_LIB'], os.environ['QEMU'],
                   '-L', os.environ['QEMU_DATA'], '-machine', 'pc', '-accel', 'tcg,thread=single', '-cpu', 'max',
                   '-m', memory, '-smp', '1', '-kernel', record['kernel']['path'], '-initrd', ','.join(modules),
                   '-icount', 'shift=0,align=off,sleep=off', '-nic', 'none', '-display', 'none', '-monitor', 'none',
                   '-serial', 'none', '-debugcon', 'file:' + str(directory / 'debugcon.log'),
                   '-device', 'isa-debug-exit,iobase=0xf4,iosize=4', '-no-reboot']
        (directory / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
        with (directory / 'qemu.stderr').open('xb') as handle:
            result = subprocess.run(command, stdout=handle, stderr=subprocess.STDOUT, timeout=90)
        raw = (directory / 'debugcon.log').read_bytes()
        lines = raw.decode('ascii').splitlines()
        verdict = judge(lines, result.returncode)
        print(directory.name, 'qemu=' + str(result.returncode), 'pass=' + str(verdict['ok']), '\n'.join(lines[-14:]), flush=True)
        if negative:
            assert result.returncode == 65 and not verdict['ok'] and 'PWAIT CONSUMER FAIL' in lines
            assert 'PWAIT FAILED consumer result' in lines and lines.count('PERFETTO WAIT GUEST FAIL') == 1
        else:
            assert verdict['ok'], verdict['problems']
            mutations = [(lines, 0), ([], 33), (lines + ['PERFETTO WAIT GUEST PASS'], 33)]
            for line in [value for value in lines if value.startswith(('PWAIT CASE ', 'PWAIT REAP '))]:
                mutations += [(lines + [line], 33), ([value for value in lines if value != line], 33)]
                for field in line.split()[2:]:
                    changed = line.replace(' ' + field, '', 1)
                    mutations.append(([changed if value == line else value for value in lines], 33))
                    key, value = field.split('=')
                    changed_value = (0 if int(value, 16) else 1) if key in ('calls', 'sleeps', 'yields') else int(value, 16) ^ 1
                    changed = line.replace(field, '%s=%08X' % (key, changed_value), 1)
                    mutations.append(([changed if item == line else item for item in lines], 33))
            assert all(not judge(changed, code)['ok'] for changed, code in mutations)
            verdict['rejected_judge_mutations'] = len(mutations)
        verdict.update({'exit': result.returncode, 'log_bytes': len(raw), 'negative': negative,
                        'log_sha256': hashlib.sha256(raw).hexdigest()})
        (directory / 'verdict.json').write_text(json.dumps(verdict, indent=2) + '\n')
        verify()
        results.append(verdict)
    assert hashlib.sha256(Path(__file__).read_bytes()).hexdigest() == judge_sha
    (args.out / 'status.json').write_text(json.dumps({'native_wait_guest_pass': True, 'content_c1_c2_c3': False,
        'manifest_sha256': hashlib.sha256(manifest_bytes).hexdigest(), 'judge_source_sha256': judge_sha,
        'results': results, 'scope': 'actual native event wait deadlines and yield-based sleep, live scheduler sleeps/yields, fault containment and Reap; no cross-thread or browser claim'}, indent=2) + '\n')
    print('PERFETTO_WAIT_GUEST_RUN_PASS positive=2 rejected_wrong_consumer=1')


if __name__ == '__main__':
    main()
