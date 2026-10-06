#!/usr/bin/env python3
"""Real BIOS ISO acceptance. No host disks, network, or existing images attached."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools'))
from qemu_runtime import qemu_environment


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--output', type=Path, default=ROOT / 'obj/x86_64-evidence')
    ap.add_argument('--runtime', type=Path, default=ROOT.parent / 'gtos-runtime')
    args = ap.parse_args()
    output = args.output.resolve()
    if output.exists():
        raise SystemExit('Use a new output directory; existing evidence is never overwritten')
    output.mkdir(parents=True)
    env = qemu_environment()
    runtime = args.runtime.resolve()
    rootless = runtime / 'root'
    qemu = shutil.which('qemu-system-x86_64')
    grub = shutil.which('grub-mkrescue')
    extra = []
    if (rootless / 'usr/bin/qemu-system-x86_64').exists():
        qemu = str(rootless / 'usr/bin/qemu-system-x86_64')
        env = qemu_environment(rootless, env)
        extra = ['-L', str(rootless / 'usr/share/qemu')]
    if (runtime / 'bin/grub-mkrescue').exists():
        grub = str(runtime / 'bin/grub-mkrescue')
    if not qemu or not grub:
        raise SystemExit('QEMU x86_64 and BIOS GRUB tools are required')
    results = []
    def command(cmd, log):
        r = subprocess.run(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (output / log).write_bytes(r.stdout)
        if r.returncode:
            raise RuntimeError(f'{cmd} failed; see {output / log}')
        return r.stdout.decode(errors='replace')
    command([qemu, '--version'], 'qemu-version.txt')
    command(['gcc', '--version'], 'gcc-version.txt')
    command(['ld', '--version'], 'ld-version.txt')
    def sources():
        paths = list((ROOT / 'arch/x86_64').rglob('*')) + [ROOT / 'tools/qemu_runtime.py']
        return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(paths)
                if p.is_file() and '__pycache__' not in p.parts}
    manifest = sources()
    (output / 'source-sha256.json').write_text(json.dumps(manifest, indent=2) + '\n')
    command(['make', '-f', 'arch/x86_64/Makefile', 'test-host'], 'host-tests.log')
    def build(label, opt='-O2', inject=0):
        build_path = output / ('build-' + label)
        command(['make', '-f', 'arch/x86_64/Makefile', f'BUILD={build_path}',
                 f'GRUB_MKRESCUE={grub}', f'OPT={opt}', f'TEST_INJECT={inject}', '-j4'], label + '-build.log')
        elf = build_path / 'kernel.elf'
        data = elf.read_bytes()
        if data[:7] != b'\x7fELF\x02\x01\x01' or int.from_bytes(data[16:18], 'little') != 2 or int.from_bytes(data[18:20], 'little') != 62:
            raise RuntimeError('Not a little-endian x86-64 ET_EXEC image')
        # Parse PHDRs directly, refusing runtime dependencies and writable code.
        phoff = int.from_bytes(data[32:40], 'little')
        size = int.from_bytes(data[54:56], 'little')
        count = int.from_bytes(data[56:58], 'little')
        loads = 0
        for i in range(count):
            p = data[phoff+i*size:phoff+(i+1)*size]
            kind, flags = int.from_bytes(p[:4], 'little'), int.from_bytes(p[4:8], 'little')
            if kind in (2, 3):
                raise RuntimeError('Unexpected DYNAMIC/INTERP segment')
            if kind == 1:
                loads += 1
                if flags & 3 == 3:
                    raise RuntimeError('Writable executable segment')
                address = int.from_bytes(p[16:24], 'little')
                memsz = int.from_bytes(p[40:48], 'little')
                if address % 4096 or memsz % 4096 or address + memsz > 0x400000:
                    raise RuntimeError('Unaligned/unbounded kernel load segment')
        if loads != 3:
            raise RuntimeError('Unexpected load-segment count')
        command(['readelf', '-ahl', str(elf)], label + '-elf.txt')
        return build_path / 'GTOS-x64.iso'
    def run(name, iso, cpu='max', memory='64M', smp='1', machine='pc', reject=None, required=()):
        debug = output / (name + '-guest.log')
        cmd = [qemu, *extra, '-machine', machine, '-accel', 'tcg', '-cpu', cpu,
               '-m', memory, '-smp', smp, '-cdrom', str(iso), '-boot', 'd',
               '-display', 'none', '-serial', 'none', '-monitor', 'none', '-nic', 'none',
               '-debugcon', 'file:' + str(debug), '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04', '-no-reboot']
        r = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        (output / (name + '-qemu.log')).write_bytes(r.stdout)
        log = debug.read_text()
        expected_exit = 35 if reject else 33
        if r.returncode != expected_exit:
            raise RuntimeError(f'{name}: exit {r.returncode}, expected {expected_exit}\n{log}')
        if reject:
            if reject not in log or 'X64 FOUNDATION PASS' in log:
                raise RuntimeError(f'{name}: rejection marker missing or false PASS\n{log}')
        else:
            markers = ['long mode', 'bounded handoff copy', 'sparse W^X supervisor mappings',
                       'WP text write', 'WP rodata write', 'NX data execution', 'null read',
                       'stack overflow on IST', 'boot-info mapping revoked', 'invalid opcode containment']
            if any(log.count('X64 PASS ' + m) != 1 for m in markers) or 'X64 FAIL' in log or log.count('X64 FOUNDATION PASS BSP-only tests=7') != 1:
                raise RuntimeError(f'{name}: missing/duplicate/failed acceptance markers\n{log}')
            faults = re.findall(r'X64 EXPECTED vector=([0-9a-f]+) error=([0-9a-f]+)', log)
            if [(int(a,16),int(b,16)) for a,b in faults] != [(14,3),(14,3),(14,17),(14,0),(14,2),(14,0),(6,0)]:
                raise RuntimeError(f'{name}: unexpected fault sequence')
        if any(marker not in log for marker in required):
            raise RuntimeError(f'{name}: required diagnostic missing')
        results.append({'name': name, 'command': cmd, 'exit': r.returncode,
                        'iso_sha256': hashlib.sha256(iso.read_bytes()).hexdigest(),
                        'guest_sha256': hashlib.sha256(debug.read_bytes()).hexdigest(), 'result': 'PASS'})
        (output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        print(name + ': PASS', flush=True)
    normal = build('o2')
    for name,cpu,mem,smp,machine in [
            ('max-16m-1','max','16M','1','pc'),
            ('qemu64-32m-1','qemu64','32M','1','pc'),
            ('max-64m-4','max','64M','4','pc'),
            ('nehalem-256m-2','Nehalem','256M','2','pc'),
            ('max-q35-512m-4','max','512M','4','q35')]:
        run(name,normal,cpu,mem,smp,machine)
    unoptimized = build('o0','-O0')
    run('o0-max-64m-4',unoptimized,smp='4')
    for name,cpu in [('no-nx','max,-nx'),('no-lm','qemu32'),('no-pae','max,-pae'),('no-msr','max,-msr'),('no-apic','max,-apic')]:
        run(name,normal,cpu=cpu,reject='X64 FAIL required CPU features')
    for inject, marker in ([(1,'boot handoff'),(2,'boot handoff'),(3,'tag size'),(4,'tag size'),(5,'unexpected exception'),(6,'unexpected exception')] + [(i,'boot handoff') for i in range(7,14)] + [(i,'unexpected exception') for i in range(14,20)] + [(i,'unsupported entry state') for i in range(20,23)]):
        iso=build('inject-'+str(inject),inject=inject)
        run('inject-'+str(inject),iso,memory='128M' if inject==13 else '64M',reject='X64 FAIL '+marker,
            required=('X64 DOUBLE FAULT on dedicated IST',) if inject==6 else ())
    after = sources()
    (output / 'source-sha256-after.json').write_text(json.dumps(after, indent=2) + '\n')
    if after != manifest:
        raise RuntimeError('Sources changed during acceptance; the evidence is not a frozen proof')
    print('source manifest before/after: MATCH', flush=True)
    print(f'x64 QEMU acceptance: {len(results)} cases PASS; evidence {output}')


if __name__ == '__main__':
    main()
