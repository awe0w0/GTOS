#!/usr/bin/env python3
"""Judge actual CPL3/ATA path operations, detached endpoints and unmounted stores."""
import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path


def judge(lines, code, phase):
    count = 1 if phase else 2
    groups = {'PATH CASE': {}, 'PATH REAP': {}}
    fields = {'PATH CASE': {'phase', 'iteration', 'checks', 'cs', 'calls', 'service', 'raw', 'mounted', 'open'},
              'PATH REAP': {'iteration', 'free', 'expected', 'open'}}
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
            if set(values) != fields[name] or values.get('iteration') not in range(count):
                problems.append('incomplete ' + name)
                continue
            index = values['iteration']
            if index in groups[name]:
                problems.append('duplicate ' + name)
            groups[name][index] = values
    if all(set(group) == set(range(count)) for group in groups.values()):
        baseline = groups['PATH REAP'][0]['expected']
        for index, value in groups['PATH CASE'].items():
            if value['phase'] != phase or value['cs'] != 0x23 or value['checks'] != (5 if phase else 57):
                problems.append('phase/context/checks')
            if value['mounted'] != int(phase == 0) or value['open'] or value['raw'] != (0 if phase else 11):
                problems.append('store/handle/raw state')
            if phase:
                if value['calls'] != 8 or value['service'] != (0 if phase == 2 else 5):
                    problems.append('unavailable endpoint layer')
            elif value['service'] < 5 or value['service'] != value['calls'] - 3:
                problems.append('actual file dispatch count')
            reap = groups['PATH REAP'][index]
            if not baseline or reap['free'] != baseline or reap['expected'] != baseline or reap['open']:
                problems.append('Reap accounting')
    else:
        problems.append('missing consumer/Reap rows')
    for text, expected in {'NATIVE PATH GUEST BOOT': 1, 'NATIVE PATH GUEST PASS': 1,
                           'PATH CONSUMER BEGIN': count, 'PATH CONSUMER PASS': count}.items():
        if lines.count(text) != expected:
            problems.append('line count ' + text)
    if code != 33 or any(any(word in line for word in ['FAIL', 'UNEXPECTED', 'ASSERT']) for line in lines):
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
    record = json.loads(manifest_bytes)
    judge_sha = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    assert record['kernel_fp_policy'] == 'NativeFpSse2' and record['source_before_after_identical']
    assert len(record['modules']) == 6 and len({(v['optimization'], v['mode'], v['negative']) for v in record['modules']}) == 6
    assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == digest for path, digest in record['inputs'].items())

    def verify():
        for entry in [record['kernel'], *record['modules']]:
            path = Path(entry['path'])
            assert args.build.resolve() in path.resolve().parents and not path.is_symlink()
            assert path.is_file() and not path.stat().st_mode & 0o222
            assert path.stat().st_size == entry['bytes'] and hashlib.sha256(path.read_bytes()).hexdigest() == entry['sha256']
        assert manifest.read_bytes() == manifest_bytes

    verify()
    results = []
    for optimization, memory, phase, negative in [(0, '32M', 0, False), (2, '64M', 0, False),
                                                 (2, '32M', 1, False), (2, '32M', 2, False), (2, '32M', 0, True)]:
        directory = args.out / ('O%d-%s-phase%d%s' % (optimization, memory, phase, '-negative' if negative else ''))
        directory.mkdir()
        module = next(v['path'] for v in record['modules'] if v['optimization'] == optimization
                      and v['mode'] == int(phase != 0) and v['negative'] == negative)
        original = (b''.join(bytes([0xA5 + i]) * 512 for i in range(8)) + bytes([0xFF]) * (2048 * 512)
                    + b''.join(bytes([0x5A + i]) * 512 for i in range(8)))
        disk = directory / 'scratch.img'
        disk.write_bytes(original)
        command = [os.environ['LOADER'], '--library-path', os.environ['QEMU_LIB'], os.environ['QEMU'],
                   '-L', os.environ['QEMU_DATA'], '-machine', 'pc', '-accel', 'tcg,thread=single', '-cpu', 'max',
                   '-m', memory, '-smp', '1', '-kernel', record['kernel']['path'], '-initrd', module,
                   '-append', ['mounted', 'unmounted', 'detached'][phase], '-icount', 'shift=0,align=off,sleep=off',
                   '-nic', 'none', '-display', 'none', '-monitor', 'none', '-serial', 'none',
                   '-debugcon', 'file:' + str(directory / 'debugcon.log'),
                   '-device', 'isa-debug-exit,iobase=0xf4,iosize=4', '-no-reboot']
        if phase == 0:
            command += ['-drive', 'file=' + str(disk) + ',format=raw,if=ide,index=0']
        (directory / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
        with (directory / 'qemu.stderr').open('xb') as handle:
            result = subprocess.run(command, stdout=handle, stderr=subprocess.STDOUT, timeout=90)
        raw = (directory / 'debugcon.log').read_bytes()
        lines = raw.decode('ascii').splitlines()
        verdict = judge(lines, result.returncode, phase)
        print(directory.name, 'qemu=' + str(result.returncode), 'pass=' + str(verdict['ok']), '\n'.join(lines[-12:]), flush=True)
        final = disk.read_bytes()
        assert len(final) == len(original) and final[:4096] == original[:4096] and final[-4096:] == original[-4096:]
        assert (final != original) == (phase == 0)
        if negative:
            assert result.returncode == 65 and not verdict['ok'] and 'PATH CONSUMER FAIL' in lines
            assert 'PATH FAILED consumer result' in lines
        else:
            assert verdict['ok'], verdict['problems']
            mutations = [(lines, 0), ([], 33), (lines + ['NATIVE PATH GUEST PASS'], 33)]
            for line in [v for v in lines if v.startswith(('PATH CASE ', 'PATH REAP '))]:
                mutations += [(lines + [line], 33), ([v for v in lines if v != line], 33)]
                for field in line.split()[2:]:
                    changed = line.replace(' ' + field, '', 1)
                    mutations.append(([changed if v == line else v for v in lines], 33))
            assert all(not judge(changed, code, phase)['ok'] for changed, code in mutations)
            verdict['rejected_judge_mutations'] = len(mutations)
        verdict.update({'exit': result.returncode, 'negative': negative, 'phase': phase,
                        'log_bytes': len(raw), 'log_sha256': hashlib.sha256(raw).hexdigest(),
                        'disk_sha256': hashlib.sha256(final).hexdigest(), 'disk_guards_intact': True})
        (directory / 'verdict.json').write_text(json.dumps(verdict, indent=2) + '\n')
        verify()
        results.append(verdict)
    assert hashlib.sha256(Path(__file__).read_bytes()).hexdigest() == judge_sha
    (args.out / 'status.json').write_text(json.dumps({'native_path_guest_pass': True, 'content_c1_c2_c3': False,
        'manifest_sha256': hashlib.sha256(manifest_bytes).hexdigest(), 'judge_source_sha256': judge_sha,
        'results': results, 'scope': 'current-source native typed paths and selected handle operations, real CPL3/ATA/Reap'}, indent=2) + '\n')
    print('NATIVE_PATH_GUEST_RUN_PASS mounted=2 unmounted=1 detached=1 rejected_wrong_consumer=1')


if __name__ == '__main__':
    main()
