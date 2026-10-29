#!/usr/bin/env python3
"""Build the current-source diagnostic V8 workload kernel.

The recipe is the one this tree already uses for its native-process diagnosis kernel (`tests/native_process_smoke.sh`), with the
observer replaced by the V8 workload observer and every translation unit compiled from the current sources. The entry symbol
addresses come from the pinned image and are written as the header the observer includes, so the kernel and the image it loads are
bound to the same bytes. No object from an earlier packet is reused.
"""
import hashlib
import json
import os
import pathlib
import subprocess

REPO = pathlib.Path(os.environ['REPO'])
OUT = pathlib.Path(os.environ['OUT'])
ELF = pathlib.Path(os.environ['ELF'])
NM = pathlib.Path(os.environ['NM'])
OBJ = OUT / 'obj'
OBJ.mkdir(parents=True, exist_ok=True)
for tool in (NM,):
    if tool.exists():
        os.chmod(str(tool), 0o555)

# The core units the native runtime is driven through, exactly as the tree's own diagnosis script names them.
SOURCES = ['src/gdt.cpp', 'src/multitasking.cpp', 'src/syscalls.cpp', 'src/hardwarecommunication/interrupts.cpp',
           'src/hardwarecommunication/port.cpp', 'src/process/native_runtime.cpp', 'src/process/native_realtime.cpp',
           'src/process/resources.cpp', 'src/process/native_surface.cpp', 'src/process/native_windows.cpp', 'src/process/native_fp.cpp',
           'src/process/elf32.cpp', 'src/memory/process_address_space.cpp', 'src/memory/paging.cpp',
           'src/memory/physical.cpp', 'src/memory/bootstrap.cpp']
ASSEMBLY = ['tests/native_process_loader.s', 'tests/native_process_user.s', 'src/process/native_fp.s',
            'src/hardwarecommunication/interruptstubs.s']
OBSERVER = 'apps/native_v8_launch/workloads/kernel.cpp'
SYMBOLS = {
    'native_v8_integration_stage': 'ENTRY_STAGE_VA',
    'native_v8_integration_result': 'ENTRY_RESULT_VA',
    'native_v8_workload_mask': 'WORKLOAD_MASK_VA',
    'native_v8_gc_callbacks': 'GC_CALLBACKS_VA',
    '_ZN12_GLOBAL__N_116largeAllocationsE': 'LARGE_ALLOCS_VA',
}


def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def run(name, argv):
    log = OUT / (name + '.log')
    result = subprocess.run([str(item) for item in argv], cwd=str(REPO), capture_output=True, text=True)
    log.write_text('$ ' + ' '.join(str(item) for item in argv) + '\n' + result.stdout + result.stderr)
    if result.returncode != 0:
        raise SystemExit('%s exited %d; full output in %s' % (name, result.returncode, log))
    return result.stdout


if not ELF.is_file():
    raise SystemExit('the pinned image is missing: ' + str(ELF))
state = {'repository': str(REPO), 'out': str(OUT), 'elf': {'path': str(ELF), 'bytes': ELF.stat().st_size, 'sha256': sha(ELF)},
         'diagnosis_script': {'path': 'tests/native_process_smoke.sh', 'sha256': sha(REPO / 'tests/native_process_smoke.sh')},
         'observer': {'path': OBSERVER, 'sha256': sha(REPO / OBSERVER)}}

# 1. The pinned image's state symbols, read from the image itself.
out = run('entry-symbols', [NM, '--defined-only', ELF])
found = {}
for line in out.splitlines():
    words = line.split()
    if len(words) == 3 and words[2] in SYMBOLS:
        found[words[2]] = int(words[0], 16)
missing = sorted(name for name in SYMBOLS if name not in found)
if missing:
    raise SystemExit('the pinned image lacks ' + ', '.join(missing))
header = OUT / 'entry-symbols.h'
header.write_text(''.join('#define %s 0x%xU\n' % (SYMBOLS[name], found[name]) for name in SYMBOLS))
state['symbols'] = {SYMBOLS[name]: hex(found[name]) for name in SYMBOLS}
state['entry_symbols_header'] = {'path': str(header), 'sha256': sha(header)}

# 2. Every translation unit from the current sources, with the flags this tree uses for this diagnosis kernel.
flags = run('kernel-flags', ['cat', 'tools/kernel-cxxflags']).split()
objects = []
for source in SOURCES + [OBSERVER]:
    name = pathlib.PurePosixPath(source).stem
    target = OBJ / (name + '.o')
    run('compile-' + name, ['g++', *flags, '-m32', '-std=c++11', '-O2', '-ffreestanding', '-nostdlib', '-fno-builtin',
                            '-fno-exceptions', '-fno-rtti', '-fno-stack-protector', '-fno-pie', '-fno-threadsafe-statics',
                            '-fno-use-cxa-atexit', '-fno-asynchronous-unwind-tables', '-Iinclude', '-I' + str(OUT),
                            '-Wno-write-strings', '-Wall', '-Wextra', '-Werror', '-c', source, '-o', str(target)])
    objects.append(target)
for source in ASSEMBLY:
    target = OBJ / (pathlib.PurePosixPath(source).stem + '.asm.o')
    run('assemble-' + pathlib.PurePosixPath(source).stem, ['as', '--32', source, '-o', str(target)])
    objects.append(target)
state['objects'] = len(objects)

# 3. Link the diagnosis image and audit its instructions.
kernel = OUT / 'v8-kernel.bin'
mapfile = OUT / 'v8-kernel.map'
run('link', ['ld', '-melf_i386', '-T', 'tests/native_process_smoke.ld', '-Map', str(mapfile), '-o', str(kernel),
             *[str(item) for item in objects]])
run('instruction-audit', ['python3', 'tools/audit-kernel-instructions.py', '--map', str(mapfile),
                          '--allow-user-range', 'native_user_start:native_user_end', str(kernel)])
state['kernel'] = {'path': str(kernel), 'bytes': kernel.stat().st_size, 'sha256': sha(kernel)}
state['instruction_audit'] = 'pass'

(OUT / 'status.json').write_text(json.dumps(state, indent=2) + '\n')
print(json.dumps(state, indent=2))
