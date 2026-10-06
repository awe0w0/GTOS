#!/usr/bin/env python3
"""Real sparse-VM guest acceptance, isolated BIOS ISO, no host disks/network.

Slice scope is reserve/commit/protect/decommit/release. Discard/reset/trim/split/
punch and user/SMP/JIT support are explicitly not conformance claims here.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
from qemu_runtime import qemu_environment
FOUNDATION = [(14, 3), (14, 3), (14, 17), (14, 0), (14, 2), (14, 0), (6, 0)]
VM_FAULTS = [(14, 0)] * 6 + [(14, 3), (14, 0), (14, 2), (14, 17), (14, 0), (14, 0), (14, 0), (14, 2)]
VM_PROBES = ['outside-left', 'after-first-run', 'before-middle', 'after-middle',
             'after-last', 'outside-right', 'read-only-write', 'none-read',
             'none-write', 'nx-execution', 'decommitted-read', 'released-read',
             'stale-va-read', 'stale-va-write']
NORMAL = [
    'giant exact reservation bytes=00000158fffff000 data=0 tables=0',
    'distant real zero isolation data=66 pt=3 pd=3 pdpt=3',
    'true outer-end page data=67 pt=4 pd=4 pdpt=3 bounds atomic restore=66+9',
    'exact guards R NONE NX and preserved backing',
    'decommit recommit zero release and 64GiB trace replay',
    '2MiB 1GiB 512GiB boundary tables',
    'shared neighbors stale foreign handles physical VA reuse',
    'invalid ranges permissions transaction and reservation quotas',
    'every allocation failure atomic new existing and 512GiB paths',
    'real bounded pool exhaustion and recovery',
    'deterministic fragmented ownership lifecycle',
    'all dynamic mappings reclaimed original protections unchanged',
]
FRAME_MARKERS = [
    'guest boot exclusions and initialization failures', 'source exclusions', 'ownership',
    'context BSP IF CR3 CR4 recursion', 'real zero isolation exhaustion release reuse roles',
    'failures exact accounting preserved boot mappings',
]


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--output', type=Path, default=ROOT / 'obj/x64-vm-evidence')
    ap.add_argument('--runtime', type=Path, default=ROOT.parent / 'gtos-runtime')
    ap.add_argument('--smoke', action='store_true', help='One O2 normal case only; not full acceptance')
    args = ap.parse_args()
    output = args.output.resolve()
    if output.exists():
        raise SystemExit('Use a fresh output directory; existing evidence is never overwritten')
    output.mkdir(parents=True)
    env = qemu_environment()
    runtime = args.runtime.resolve()
    rootless = runtime / 'root'
    qemu, grub = shutil.which('qemu-system-x86_64'), shutil.which('grub-mkrescue')
    extra = []
    if (rootless / 'usr/bin/qemu-system-x86_64').exists():
        qemu = str(rootless / 'usr/bin/qemu-system-x86_64')
        env = qemu_environment(rootless)
        extra = ['-L', str(rootless / 'usr/share/qemu')]
    if (runtime / 'bin/grub-mkrescue').exists():
        grub = str(runtime / 'bin/grub-mkrescue')
    if not qemu or not grub:
        raise SystemExit('QEMU x86_64 and BIOS GRUB tools are required')
    commands, results = [], []

    def command(cmd, log):
        commands.append(cmd)
        r = subprocess.run(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (output / log).write_bytes(r.stdout)
        if r.returncode:
            raise RuntimeError(f'{cmd} failed; see {output / log}')
        return r.stdout.decode(errors='replace')

    def sources():
        files = [p for p in (ROOT / 'arch/x86_64').rglob('*')
                 if p.is_file() and '__pycache__' not in p.parts]
        helper = ROOT / 'tools/qemu_runtime.py'
        if helper.exists():
            files.append(helper)
        return {str(p.relative_to(ROOT)): digest(p) for p in sorted(files)}

    manifest = sources()
    (output / 'source-sha256.json').write_text(json.dumps(manifest, indent=2) + '\n')
    command([qemu, '--version'], 'qemu-version.txt')
    command(['gcc', '--version'], 'gcc-version.txt')
    command(['ld', '--version'], 'ld-version.txt')
    command([grub, '--version'], 'grub-version.txt')
    command(['make', '-f', 'arch/x86_64/Makefile', 'test-host', f'BUILD={output / "host"}'], 'host-tests.log')

    def build(label, opt='-O2', injection=0, small=False):
        path = output / ('build-' + label)
        command(['make', '-f', 'arch/x86_64/Makefile', f'BUILD={path}',
                 f'GRUB_MKRESCUE={grub}', f'OPT={opt}', 'VM_TEST=1',
                 f'VM_TEST_INJECT={injection}', f'FRAME_TEST_SMALL={int(small)}', '-j4'], label + '-build.log')
        elf = (path / 'kernel.elf').read_bytes()
        if elf[:7] != b'\x7fELF\x02\x01\x01' or int.from_bytes(elf[16:18], 'little') != 2:
            raise RuntimeError('VM guest is not fixed little-endian ELF64 ET_EXEC')
        phoff, entsize, count = (int.from_bytes(elf[a:b], 'little') for a, b in [(32, 40), (54, 56), (56, 58)])
        loads = 0
        for i in range(count):
            p = elf[phoff + i*entsize:phoff + (i+1)*entsize]
            kind, flags = int.from_bytes(p[:4], 'little'), int.from_bytes(p[4:8], 'little')
            if kind in (2, 3):
                raise RuntimeError('Unexpected dynamic dependency in VM guest')
            if kind == 1:
                loads += 1
                va, size = int.from_bytes(p[16:24], 'little'), int.from_bytes(p[40:48], 'little')
                if flags & 3 == 3 or va % 4096 or size % 4096 or va + size > 0x400000:
                    raise RuntimeError('Invalid VM guest load segment')
        if loads != 3:
            raise RuntimeError('Unexpected load-segment count')
        command(['readelf', '-ahl', str(path / 'kernel.elf')], label + '-elf.txt')
        return path

    def run(name, path, cpu='max', memory='64M', smp='1', machine='pc', reject=None, small=False):
        debug, iso = output / (name + '-guest.log'), path / 'GTOS-x64.iso'
        cmd = [qemu, *extra, '-machine', machine, '-accel', 'tcg', '-cpu', cpu,
               '-m', memory, '-smp', smp, '-cdrom', str(iso), '-boot', 'd',
               '-display', 'none', '-serial', 'none', '-monitor', 'none', '-nic', 'none',
               '-debugcon', 'file:' + str(debug), '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04', '-no-reboot']
        commands.append(cmd)
        start = time.monotonic()
        try:
            r = subprocess.run(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900)
        except subprocess.TimeoutExpired as exc:
            (output / (name + '-qemu.log')).write_bytes(exc.stdout or b'')
            raise RuntimeError(f'{name}: guest timed out') from exc
        (output / (name + '-qemu.log')).write_bytes(r.stdout)
        log = debug.read_text()
        if r.returncode != (35 if reject else 33):
            raise RuntimeError(f'{name}: exit {r.returncode}, expected {35 if reject else 33}\n{log}')
        if log.count('X64 FOUNDATION PASS BSP-only tests=7') != 1:
            raise RuntimeError(f'{name}: original seven-probe foundation did not pass')
        old_faults = [(int(a, 16), int(b, 16)) for a, b in re.findall(
            r'^X64 EXPECTED vector=([0-9a-f]+) error=([0-9a-f]+)', log, re.M)]
        if old_faults != FOUNDATION:
            raise RuntimeError(f'{name}: original exact fault sequence changed')
        if any(log.count('X64 FRAME PASS ' + m) != 1 for m in FRAME_MARKERS):
            raise RuntimeError(f'{name}: frame-pool regression markers missing or duplicated')
        frame_end = re.findall(r'^X64 FRAME POOL PASS BSP-only managed=([0-9a-f]+)$', log, re.M)
        if len(frame_end) != 1 or int(frame_end[0], 16) != (7 if small else 2048):
            raise RuntimeError(f'{name}: real managed-pool capacity wrong')
        if reject:
            if reject not in log or 'X64 VM SERVICE PASS' in log:
                raise RuntimeError(f'{name}: missing rejection or false VM success\n{log}')
        else:
            expected = (['reduced real pool seven frames exact OOM recovery', NORMAL[-1]] if small else NORMAL)
            if any(log.count('X64 VM PASS ' + m + '\n') != 1 for m in expected) or 'FAIL' in log:
                raise RuntimeError(f'{name}: missing/duplicate/failed VM markers\n{log}')
            faults = [(int(a, 16), int(b, 16)) for a, b in re.findall(
                r'^X64 VM EXPECTED vector=([0-9a-f]+) error=([0-9a-f]+)', log, re.M)]
            probes = re.findall(r'^X64 VM PROBE (.+)$', log, re.M)
            if faults != ([] if small else VM_FAULTS) or probes != ([] if small else VM_PROBES):
                raise RuntimeError(f'{name}: exact VM fault sequence changed: {faults}, {probes}')
            end = re.findall(r'^X64 VM SERVICE PASS BSP-only data=0 tables=0 live=0 faults=([0-9a-f]+) rollback=([0-9a-f]+) managed=([0-9a-f]+)$', log, re.M)
            expected_end = (0, 0, 7) if small else (14, 284, 2048)
            if len(end) != 1 or tuple(int(n, 16) for n in end[0]) != expected_end:
                raise RuntimeError(f'{name}: final exact accounting wrong')
            if log.count('X64 VM LIMITS supervisor NX-only discard reset trim split punch deferred\n') != 1:
                raise RuntimeError(f'{name}: scope declaration missing')
        results.append({'name': name, 'command': cmd, 'cpu': cpu, 'memory': memory, 'smp': smp,
                        'machine': machine, 'expected_exit': 35 if reject else 33, 'actual_exit': r.returncode,
                        'expected_vm_faults': 0 if small else (None if reject else 14),
                        'expected_rollback_cases': 0 if small else (None if reject else 284),
                        'elapsed_seconds': round(time.monotonic() - start, 3),
                        'elf_sha256': digest(path / 'kernel.elf'), 'iso_sha256': digest(iso),
                        'guest_sha256': digest(debug), 'result': 'PASS'})
        (output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        (output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        print(name + ': PASS', flush=True)

    matrix = [('max-16m-1', 'max', '16M', '1', 'pc'),
              ('qemu64-32m-1', 'qemu64', '32M', '1', 'pc'),
              ('max-64m-4', 'max', '64M', '4', 'pc'),
              ('nehalem-256m-2', 'Nehalem', '256M', '2', 'pc'),
              ('max-q35-512m-4', 'max', '512M', '4', 'q35')]
    for opt in (('-O2',) if args.smoke else ('-O0', '-O2')):
        label = opt[1:].lower()
        normal = build(label, opt)
        for name, cpu, mem, smp, machine in (matrix[2:3] if args.smoke else matrix):
            run(label + '-' + name, normal, cpu, mem, smp, machine)
        if args.smoke:
            continue
        reduced = build(label + '-small', opt, small=True)
        for memory in ('16M', '64M'):
            run(label + '-small-' + memory, reduced, memory=memory, small=True)
        for injection, reject in [(1, 'X64 FAIL unexpected exception'),
                                  (2, 'X64 VM FAIL stats'), (3, 'X64 VM FAIL zero-before-use'),
                                  (4, 'X64 VM FAIL stats')] + [(i, 'X64 FAIL unexpected exception') for i in range(11, 17)]:
            bad = build(label + '-inject-' + str(injection), opt, injection=injection)
            run(label + '-inject-' + str(injection), bad, reject=reject)
    after = sources()
    (output / 'source-sha256-after.json').write_text(json.dumps(after, indent=2) + '\n')
    (output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    if manifest != after:
        raise RuntimeError('Sources changed during acceptance; this is not a frozen proof')
    scope = 'SMOKE ONLY' if args.smoke else 'FULL SLICE ACCEPTANCE'
    print(f'x64 sparse VM QEMU {scope}: {len(results)} cases PASS; source manifest MATCH; evidence {output}')


if __name__ == '__main__':
    main()
