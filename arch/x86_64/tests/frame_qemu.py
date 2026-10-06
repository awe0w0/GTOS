#!/usr/bin/env python3
"""Real map-derived bounded-pool acceptance; BIOS ISO only, no disks/network."""
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
    ap.add_argument('--output', type=Path, default=ROOT / 'obj/x64-frame-evidence')
    ap.add_argument('--runtime', type=Path, default=ROOT.parent / 'gtos-runtime')
    args = ap.parse_args()
    output = args.output.resolve()
    if output.exists():
        raise SystemExit('Use a new output directory; existing evidence is never overwritten')
    output.mkdir(parents=True)
    env = qemu_environment()
    runtime = args.runtime.resolve()
    rootless = runtime / 'root'
    qemu, grub = shutil.which('qemu-system-x86_64'), shutil.which('grub-mkrescue')
    extra = []
    if (rootless / 'usr/bin/qemu-system-x86_64').exists():
        qemu = str(rootless / 'usr/bin/qemu-system-x86_64')
        env = qemu_environment(rootless, env)
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
        paths = list((ROOT / 'arch/x86_64').rglob('*')) + [ROOT / 'tools/qemu_runtime.py']
        return {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(paths)
                if p.is_file() and '__pycache__' not in p.parts}
    manifest = sources()
    (output / 'source-sha256.json').write_text(json.dumps(manifest, indent=2) + '\n')
    command([qemu, '--version'], 'qemu-version.txt')
    command(['gcc', '--version'], 'gcc-version.txt')
    command(['ld', '--version'], 'ld-version.txt')
    command(['make', '-f', 'arch/x86_64/Makefile', 'test-host'], 'host-tests.log')
    def build(label, opt='-O2', inject=0, small=False, modules=False):
        path = output / ('build-' + label)
        command(['make', '-f', 'arch/x86_64/Makefile', f'BUILD={path}',
                 f'GRUB_MKRESCUE={grub}', f'OPT={opt}', f'FRAME_TEST_INJECT={inject}',
                 f'FRAME_TEST_SMALL={int(small)}', '-j4'], label + '-build.log')
        if modules:
            for n, size in enumerate((5003, 8221)):
                (path / f'iso/boot/module{n}.bin').write_bytes(bytes((i % 251 for i in range(size))))
            (path / 'iso/boot/grub/grub.cfg').write_text('set timeout=0\nset default=0\nmenuentry "frame modules" {\n multiboot2 /boot/kernel.elf\n module2 /boot/module0.bin partial-zero\n module2 /boot/module1.bin partial-one\n boot\n}\n')
            command([grub, '-o', str(path / 'GTOS-x64.iso'), str(path / 'iso')], label + '-modules-build.log')
        command(['readelf', '-ahl', str(path / 'kernel.elf')], label + '-elf.txt')
        return path
    def run(name, path, cpu='max', memory='64M', smp='1', machine='pc', reject=None, small=False, modules=False, limited=False):
        debug = output / (name + '-guest.log')
        iso = path / 'GTOS-x64.iso'
        cmd = [qemu, *extra, '-machine', machine, '-accel', 'tcg', '-cpu', cpu,
               '-m', memory, '-smp', smp, '-cdrom', str(iso), '-boot', 'd',
               '-display', 'none', '-serial', 'none', '-monitor', 'none', '-nic', 'none',
               '-debugcon', 'file:' + str(debug), '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04', '-no-reboot']
        commands.append(cmd)
        r = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
        (output / (name + '-qemu.log')).write_bytes(r.stdout)
        log = debug.read_text()
        if r.returncode != (35 if reject else 33):
            raise RuntimeError(f'{name}: bad exit {r.returncode}\n{log}')
        if log.count('X64 FOUNDATION PASS BSP-only tests=7') != 1:
            raise RuntimeError(f'{name}: original foundation probes incomplete')
        faults = re.findall(r'X64 EXPECTED vector=([0-9a-f]+) error=([0-9a-f]+)', log)
        if [(int(a,16),int(b,16)) for a,b in faults] != [(14,3),(14,3),(14,17),(14,0),(14,2),(14,0),(6,0)]:
            raise RuntimeError(f'{name}: original fault sequence changed')
        if reject:
            if reject not in log or 'X64 FRAME POOL PASS' in log:
                raise RuntimeError(f'{name}: missing rejection or false pass\n{log}')
        else:
            markers = ['guest boot exclusions and initialization failures', 'source exclusions', 'ownership',
                       'context BSP IF CR3 CR4 recursion', 'real zero isolation exhaustion release reuse roles',
                       'failures exact accounting preserved boot mappings']
            if any(log.count('X64 FRAME PASS ' + m) != 1 for m in markers) or 'FAIL' in log:
                raise RuntimeError(f'{name}: missing/duplicate/failed frame markers\n{log}')
            end = re.findall(r'X64 FRAME POOL PASS BSP-only managed=([0-9a-f]+)', log)
            if len(end)!=1:
                raise RuntimeError(f'{name}: missing/duplicate final capacity')
            count = int(end[0],16)
            ownership = re.findall(r'eligible=([0-9a-f]+) managed=([0-9a-f]+) aliases=([0-9a-f]+) borrowed-tables=([0-9a-f]+)', log)
            if len(ownership)!=1:
                raise RuntimeError(f'{name}: missing/duplicate ownership accounting')
            eligible, managed, aliases, borrowed = (int(n,16) for n in ownership[0])
            if managed!=count or aliases!=count or borrowed!=35 or managed!=min(7 if small else 2048,eligible):
                raise RuntimeError(f'{name}: inconsistent ownership accounting')
            if limited:
                if not 512<=count<2048:
                    raise RuntimeError(f'{name}: did not exercise actual reduced firmware capacity')
            elif count!=(7 if small else 2048):
                raise RuntimeError(f'{name}: incorrect bounded capacity')
            if modules and 'modules=0000000000000002' not in log:
                raise RuntimeError(f'{name}: actual GRUB module exclusions untested')
        results.append({'name': name, 'command': cmd, 'exit': r.returncode,
                        'elf_sha256': hashlib.sha256((path / 'kernel.elf').read_bytes()).hexdigest(),
                        'iso_sha256': hashlib.sha256(iso.read_bytes()).hexdigest(),
                        'guest_sha256': hashlib.sha256(debug.read_bytes()).hexdigest(), 'result': 'PASS'})
        (output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        print(name + ': PASS', flush=True)
    matrix = [('max-16m-1','max','16M','1','pc'), ('qemu64-32m-1','qemu64','32M','1','pc'),
              ('max-64m-4','max','64M','4','pc'), ('nehalem-256m-2','Nehalem','256M','2','pc'),
              ('max-q35-512m-4','max','512M','4','q35')]
    for opt in ('-O0','-O2'):
        label=opt[1:].lower()
        normal=build(label,opt)
        run(label+'-max-4m-1',normal,memory='4M',limited=True)
        for name,cpu,mem,smp,machine in matrix:
            run(label+'-'+name,normal,cpu,mem,smp,machine)
        reduced=build(label+'-small',opt,small=True)
        for mem in ('16M','64M'):
            run(label+'-small-'+mem,reduced,memory=mem,small=True)
        module=build(label+'-modules',opt,modules=True)
        run(label+'-modules',module,memory='16M',modules=True)
        for inject,marker in [(1,'zero-before-use'),(2,'stats audit'),(3,'stats audit'),(4,'stats audit')]:
            bad=build(label+'-inject-'+str(inject),opt,inject=inject)
            run(label+'-inject-'+str(inject),bad,reject='X64 FRAME FAIL '+marker)
    after=sources()
    (output / 'source-sha256-after.json').write_text(json.dumps(after, indent=2) + '\n')
    (output / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    if after != manifest:
        raise RuntimeError('Sources changed during acceptance; not a frozen proof')
    print(f'x64 frame QEMU acceptance: {len(results)} cases PASS; frozen manifest MATCH; evidence {output}')


if __name__ == '__main__':
    main()
