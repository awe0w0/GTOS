#!/usr/bin/env python3
"""Qualify actual Perfetto CPU clock calls in current-source CPL3 lifetimes."""
import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path


def judge(lines, code):
    groups = {'PCPU CASE': {}, 'PCPU REAP': {}}
    fields = {'PCPU CASE': {'mode', 'checks', 'cs', 'cpu', 'wall', 'calls', 'start', 'end', 'process', 'elapsed'},
              'PCPU REAP': {'mode', 'free', 'expected'}}
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
        baseline = groups['PCPU REAP'][0]['expected']
        for mode, value in groups['PCPU CASE'].items():
            if value['checks'] != 12 or value['cs'] != 0x23 or value['calls'] != value['cpu'] + value['wall'] + 3:
                problems.append('comparisons/context/queries ' + str(mode))
            if not (value['cpu'] >= 6 and value['wall'] >= 3 and value['end'] >= value['start']
                    and value['end'] - value['start'] >= (3 + mode) * 10000
                    and value['process'] >= value['end']
                    and value['end'] - value['start'] <= value['elapsed'] <= 2000000):
                problems.append('CPU/wall bounds ' + str(mode))
            reap = groups['PCPU REAP'][mode]
            if not baseline or reap['free'] != baseline or reap['expected'] != baseline:
                problems.append('Reap accounting ' + str(mode))
    else:
        problems.append('missing case/reap records')
    for text, count in {'PERFETTO CPU GUEST BOOT': 1, 'PERFETTO CPU GUEST PASS': 1,
                        'PCPU CONSUMER BEGIN': 3, 'PCPU CONSUMER PASS': 3}.items():
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
    assert record['component'] == 'cpu' and record['kernel_fp_policy'] == 'NativeFpSse2'
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
            assert result.returncode == 65 and not verdict['ok'] and 'PCPU CONSUMER FAIL' in lines
            assert 'PCPU FAILED consumer result' in lines and lines.count('PERFETTO CPU GUEST FAIL') == 1
        else:
            assert verdict['ok'], verdict['problems']
            mutations = [(lines, 0), ([], 33), (lines + ['PERFETTO CPU GUEST PASS'], 33)]
            for line in [value for value in lines if value.startswith(('PCPU CASE ', 'PCPU REAP '))]:
                mutations += [(lines + [line], 33), ([value for value in lines if value != line], 33)]
                for field in line.split()[2:]:
                    changed = line.replace(' ' + field, '', 1)
                    mutations.append(([changed if value == line else value for value in lines], 33))
                    key, value = field.split('=')
                    if key in ['checks', 'cs', 'calls', 'free', 'expected']:
                        changed = line.replace(field, '%s=%08X' % (key, int(value, 16) ^ 1), 1)
                        mutations.append(([changed if item == line else item for item in lines], 33))
            assert all(not judge(changed, code)['ok'] for changed, code in mutations)
            verdict['rejected_judge_mutations'] = len(mutations)
        verdict.update({'exit': result.returncode, 'log_bytes': len(raw), 'negative': negative,
                        'log_sha256': hashlib.sha256(raw).hexdigest()})
        (directory / 'verdict.json').write_text(json.dumps(verdict, indent=2) + '\n')
        verify()
        results.append(verdict)
    assert hashlib.sha256(Path(__file__).read_bytes()).hexdigest() == judge_sha
    (args.out / 'status.json').write_text(json.dumps({'native_cpu_time_guest_pass': True, 'content_c1_c2_c3': False,
        'manifest_sha256': hashlib.sha256(manifest_bytes).hexdigest(), 'judge_source_sha256': judge_sha,
        'results': results, 'scope': 'self-only sampled CPU time, actual CPL3 int80 and repeated Reap; no browser claim'}, indent=2) + '\n')
    print('PERFETTO_CPU_GUEST_RUN_PASS positive=2 rejected_wrong_consumer=1')


if __name__ == '__main__':
    main()
