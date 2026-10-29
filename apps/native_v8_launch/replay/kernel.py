#!/usr/bin/env python3
"""Build the current-source diagnostic V8 workload kernel and record what is bound together.

The recipe is the one this tree already uses for its native-process diagnosis kernel (`tests/native_process_smoke.sh`), with the
observer replaced by the V8 workload observer and every translation unit compiled from the current sources. The entry symbol
addresses come from the pinned image and are written as the header the observer includes, so the kernel and the image it loads are
bound to the same bytes. No object from an earlier packet is reused, and nothing here is fixed up silently: the tools must already
be executable, and the caller's expected image identity must match before anything is built.

The record names the translation units and their bytes, the headers each unit actually needs (from the compiler's own dependency
files), the flags, the tools with their versions and modes, the symbol header, the kernel, and a digest over all of it, so a guest
run can be tied to exactly this build.

Exit status: 0 built, 2 refused to start, 3 the pinned image is not the expected one.
"""
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys

REPO = pathlib.Path(os.environ['REPO'])
OUT = pathlib.Path(os.environ['OUT'])
ELF = pathlib.Path(os.environ['ELF'])
NM = pathlib.Path(os.environ['NM'])
OBJ = OUT / 'obj'
PROFILE = 'diagnosis-current-sources-O2'

# The core units the native runtime is driven through, exactly as the tree's own diagnosis script names them.
SOURCES = ['src/gdt.cpp', 'src/multitasking.cpp', 'src/syscalls.cpp', 'src/hardwarecommunication/interrupts.cpp',
           'src/hardwarecommunication/port.cpp', 'src/process/native_runtime.cpp', 'src/process/native_realtime.cpp',
           'src/process/resources.cpp', 'src/process/native_surface.cpp', 'src/process/native_windows.cpp',
           'src/process/native_fp.cpp', 'src/process/elf32.cpp', 'src/memory/process_address_space.cpp',
           'src/memory/paging.cpp', 'src/memory/physical.cpp', 'src/memory/bootstrap.cpp']
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


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def run(name, argv, capture=True):
    log = OUT / (name + '.log')
    result = subprocess.run([str(item) for item in argv], cwd=str(REPO), capture_output=capture, text=True)
    if capture:
        log.write_text('$ ' + ' '.join(str(item) for item in argv) + '\n' + result.stdout + result.stderr)
    if result.returncode != 0:
        raise SystemExit('%s exited %d; full output in %s' % (name, result.returncode, log))
    return result.stdout


def tool(name, path):
    """A tool the caller staged: it must be a file that already carries an execute bit. This script never fixes a mode."""
    path = pathlib.Path(path)
    if not path.is_file():
        raise SystemExit('%s is missing at %s; the supply step stages it' % (name, path))
    mode = os.stat(str(path)).st_mode
    if not mode & 0o111:
        raise SystemExit('%s at %s carries no execute bit; the supply step must provide it, this script does not chmod tools' %
                         (name, path))
    version = subprocess.run([str(path), '--version'], capture_output=True, text=True).stdout.splitlines()
    return {'name': name, 'path': str(path), 'sha256': sha(path), 'bytes': path.stat().st_size,
            'mode': oct(mode & 0o777), 'version': version[0] if version else None}


for stale in (OUT / 'status.json',):
    if stale.exists():
        raise SystemExit('%s already exists; use a fresh run directory instead of overwriting evidence' % stale)
OBJ.mkdir(parents=True, exist_ok=True)

if not ELF.is_file():
    raise SystemExit('the pinned image is missing: ' + str(ELF))
want_sha = os.environ.get('EXPECT_ELF_SHA256', '')
want_bytes = os.environ.get('EXPECT_ELF_BYTES', '')
elf_sha, elf_bytes = sha(ELF), ELF.stat().st_size
if (want_sha and elf_sha != want_sha) or (want_bytes and elf_bytes != int(want_bytes)):
    print('kernel: the image is not the pinned one (sha256 %s, %d bytes; expected %s, %s bytes)' %
          (elf_sha, elf_bytes, want_sha or 'any', want_bytes or 'any'), file=sys.stderr)
    raise SystemExit(3)

state = {'repository': str(REPO), 'out': str(OUT), 'profile': PROFILE,
         'elf': {'path': str(ELF), 'bytes': elf_bytes, 'sha256': elf_sha, 'matches_expected': not want_sha or elf_sha == want_sha},
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
state['entry_symbols_sha256'] = sha(header)

# 2. Every translation unit from the current sources, with the flags this tree uses for this diagnosis kernel.
tools = {'nm': tool('llvm-nm', NM)}
flags = run('kernel-flags', ['cat', 'tools/kernel-cxxflags']).split()
tools['g++'] = tool('g++', shutil.which('g++') or '')
tools['as'] = tool('as', shutil.which('as') or '')
tools['ld'] = tool('ld', shutil.which('ld') or '')
sources, headers, objects = [], {}, []
for source in SOURCES + [OBSERVER]:
    name = pathlib.PurePosixPath(source).stem
    target = OBJ / (name + '.o')
    dependencies = OBJ / (name + '.d')
    run('compile-' + name, ['g++', *flags, '-m32', '-std=c++11', '-O2', '-ffreestanding', '-nostdlib', '-fno-builtin',
                            '-fno-exceptions', '-fno-rtti', '-fno-stack-protector', '-fno-pie', '-fno-threadsafe-statics',
                            '-fno-use-cxa-atexit', '-fno-asynchronous-unwind-tables', '-Iinclude', '-I' + str(OUT),
                            '-Wno-write-strings', '-Wall', '-Wextra', '-Werror', '-MD', '-MF', str(dependencies),
                            '-c', source, '-o', str(target)])
    objects.append(target)
    prerequisites = []
    for line in dependencies.read_text().replace('\\\n', ' ').split():
        if line.endswith(':') or line.endswith('.d'):
            continue
        prerequisites.append(line.rstrip('\\'))
    for prerequisite in prerequisites[1:]:
        path = pathlib.Path(prerequisite)
        if path.is_file():
            headers[prerequisite] = sha(path)
    sources.append({'path': source, 'sha256': sha(REPO / source), 'dependencies': len(prerequisites) - 1,
                    'dependencies_sha256': digest(sorted(prerequisites))})
for source in ASSEMBLY:
    target = OBJ / (pathlib.PurePosixPath(source).stem + '.asm.o')
    run('assemble-' + pathlib.PurePosixPath(source).stem, ['as', '--32', source, '-o', str(target)])
    objects.append(target)
    sources.append({'path': source, 'sha256': sha(REPO / source), 'dependencies': 0, 'dependencies_sha256': digest([])})
state['sources'] = sources
state['headers'] = {'count': len(headers), 'digest': digest(headers),
                    'hashes': dict(sorted(headers.items())) if len(headers) <= 500 else 'digest only (more than 500 headers)'}
state['flags'] = flags
state['tools'] = tools
state['objects'] = len(objects)
state['sources_digest'] = digest([{'path': item['path'], 'sha256': item['sha256']} for item in sources])
state['tools_digest'] = digest([{'name': item['name'], 'sha256': item['sha256'], 'version': item['version']} for item in tools.values()])

# 3. Link the diagnosis image and audit its instructions.
kernel = OUT / 'v8-kernel.bin'
mapfile = OUT / 'v8-kernel.map'
run('link', ['ld', '-melf_i386', '-T', 'tests/native_process_smoke.ld', '-Map', str(mapfile), '-o', str(kernel),
             *[str(item) for item in objects]])
run('instruction-audit', ['python3', 'tools/audit-kernel-instructions.py', '--map', str(mapfile),
                          '--allow-user-range', 'native_user_start:native_user_end', str(kernel)])
state['kernel'] = {'path': str(kernel), 'bytes': kernel.stat().st_size, 'sha256': sha(kernel)}
state['instruction_audit'] = 'pass'
state['build_digest'] = digest({'profile': PROFILE, 'sources_digest': state['sources_digest'], 'tools_digest': state['tools_digest'],
                                'elf_sha256': elf_sha, 'entry_symbols_sha256': state['entry_symbols_sha256'],
                                'kernel_sha256': state['kernel']['sha256'], 'flags': flags})

(OUT / 'status.json').write_text(json.dumps(state, indent=2) + '\n')
print(json.dumps({key: state[key] for key in ('profile', 'elf', 'entry_symbols_sha256', 'objects', 'sources_digest', 'tools_digest',
                                              'kernel', 'instruction_audit', 'build_digest')}, indent=2))
