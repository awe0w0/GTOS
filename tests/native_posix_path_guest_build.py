#!/usr/bin/env python3
"""Build current-source POSIX-subset consumers and the real CPL3/ATA endpoint."""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--clang-bin', required=True, type=Path)
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
args.out.mkdir(parents=True, exist_ok=False)
product = repo / 'apps/native_libxml_file'
sdk = repo / 'apps/native_v8_runtime'
inputs, commands = {}, []


def bind(path):
    path = path.resolve()
    if str(path) not in inputs:
        inputs[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()


def run(name, command):
    command = [str(value) for value in command]
    commands.append(command)
    result = subprocess.run(command, cwd=repo, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (args.out / (name + '.log')).write_text(result.stdout)
    assert result.returncode == 0, name + '\n' + result.stdout
    return result.stdout


def dependencies(path):
    for value in Path(str(path) + '.d').read_text().replace('\\\n', ' ').split(':', 1)[1].split():
        bind(Path(value))


def identity(path):
    return {'path': str(path), 'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


flags = ['--target=i686-unknown-none-elf', '-D__GTOS__=1', '-ffreestanding', '-fno-builtin',
         '-msse2', '-mfpmath=sse', '-mno-mmx', '-mno-avx', '-nostdinc', '-fno-stack-protector',
         '-fno-pic', '-fno-pie', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
         '-fno-exceptions', '-fno-rtti', '-std=c++17', '-ffunction-sections', '-fdata-sections',
         '-I' + str(repo / 'include'), '-I' + str(product / 'include'),
         '-isystem' + str(sdk / 'sdk'), '-isystem' + str(sdk / 'c-sdk'),
         '-isystem' + str(args.clang_bin.parent / 'lib/clang/24/include')]
layout_source = repo / 'apps/native_vm_probe/linker.ld'
bind(layout_source)
layout = args.out / 'module.ld'
layout.write_text(layout_source.read_text().replace('native_vm_record', 'native_path_record'))
bind(layout)
start = product / 'probe/exit-start.s'
bind(start)
run('start', [args.clang_bin / 'clang', '--target=i686-unknown-none-elf', '-c', start, '-o', args.out / 'start.o'])
modules = []
for optimization in (0, 2):
    directory = args.out / ('O' + str(optimization))
    directory.mkdir()
    objects = []
    for name, source in [('file', product / 'posix_file.cpp'), ('bytes', product / 'link_closure/min_closure.cpp')]:
        bind(source)
        output = directory / (name + '.o')
        run(directory.name + '-' + name, [args.clang_bin / 'clang', *flags, '-O' + str(optimization),
            '-MD', '-MF', str(output) + '.d', '-c', source, '-o', output])
        dependencies(output)
        assert 'GtosPosixFileTestCall' not in run(directory.name + '-' + name + '-symbols', ['/usr/bin/nm', '-u', output])
        objects.append(output)
    source = product / 'probe/path_suite.cpp'
    bind(source)
    for mode, negative in [(0, False), (1, False), (0, True)]:
        name = 'mode' + str(mode) + ('-bad' if negative else '')
        output = directory / (name + '.o')
        run(directory.name + '-' + name, [args.clang_bin / 'clang', *flags, '-O' + str(optimization),
            '-DGTOS_PATH_GUEST_MODE=' + str(mode), *(['-DGTOS_PATH_GUEST_WRONG_EXPECTATION'] if negative else []),
            '-MD', '-MF', str(output) + '.d', '-c', source, '-o', output])
        dependencies(output)
        image = directory / (name + '.elf')
        run(directory.name + '-' + name + '-link', [args.clang_bin / 'ld.lld', '-m', 'elf_i386', '-static',
            '-e', '_start', '-T', layout, '--gc-sections', '-o', image, args.out / 'start.o', *objects, output])
        assert not run(directory.name + '-' + name + '-undefined', ['/usr/bin/nm', '-u', image]).strip()
        data = image.read_bytes()
        assert data[:5] == b'\x7fELF\x01' and struct.unpack_from('<H', data, 18)[0] == 3 and len(data) <= 65536
        assert '40020000 D native_path_record' in run(directory.name + '-' + name + '-symbols', ['/usr/bin/nm', '-n', image])
        image.chmod(0o444)
        modules.append({'optimization': optimization, 'mode': mode, 'negative': negative, **identity(image)})

sources = ['src/gdt.cpp', 'src/multitasking.cpp', 'src/syscalls.cpp',
           'src/hardwarecommunication/interrupts.cpp', 'src/hardwarecommunication/port.cpp',
           'src/process/native_runtime.cpp', 'src/process/native_realtime.cpp', 'src/process/resources.cpp',
           'src/process/native_surface.cpp', 'src/process/native_windows.cpp', 'src/process/native_fp.cpp',
           'src/process/native_files.cpp', 'src/process/elf32.cpp', 'src/memory/process_address_space.cpp',
           'src/memory/paging.cpp', 'src/memory/physical.cpp', 'src/memory/bootstrap.cpp',
           'src/storage/filestore.cpp', 'src/storage/littlefs_port.cpp', 'src/storage/littlefs/lfs.c',
           'src/storage/littlefs/lfs_util.c', 'src/drivers/ata.cpp',
           'apps/native_v8_runtime/runtime/memory.cc',
           'tests/i386_pure_virtual_runtime.cpp', 'tests/native_posix_path_kernel.cpp']
assembly = ['tests/native_process_loader.s', 'tests/native_process_user.s',
            'src/process/native_fp.s', 'src/hardwarecommunication/interruptstubs.s']
bind(repo / 'tools/kernel-cxxflags')
policy = (repo / 'tools/kernel-cxxflags').read_text().split()
directory = args.out / 'kernel'
directory.mkdir()
objects = []
for source in sources:
    path = repo / source
    bind(path)
    output = directory / (path.stem + '.o')
    is_c = path.suffix == '.c'
    extra = ['-std=c99', '-DLFS_DEFINES=storage/littlefs_port.h'] if is_c else ['-std=c++11', '-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics',
                            '-fno-use-cxa-atexit', '-Wno-write-strings']
    run('kernel-' + path.stem, ['/usr/bin/gcc' if is_c else '/usr/bin/g++', '-m32', '-O2', '-ffreestanding',
        '-nostdlib', '-fno-builtin', '-fno-stack-protector', '-fno-pie', '-fno-asynchronous-unwind-tables',
        '-ffunction-sections', '-fdata-sections', '-Iinclude', '-Iinclude/storage/littlefs-sdk',
        '-Iinclude/storage/littlefs', '-Wall', '-Wextra', '-Werror', *extra, *policy,
        '-MD', '-MF', str(output) + '.d', '-c', path, '-o', output])
    dependencies(output)
    objects.append(output)
for source in assembly:
    path = repo / source
    bind(path)
    output = directory / (path.stem + '.asm.o')
    run('assemble-' + path.stem, ['/usr/bin/as', '--32', path, '-o', output])
    objects.append(output)
kernel = directory / 'kernel.bin'
mapfile = directory / 'kernel.map'
linker = repo / 'tests/native_process_smoke.ld'
bind(linker)
run('kernel-link', ['/usr/bin/ld', '-melf_i386', '--gc-sections', '-T', linker, '-Map', mapfile, '-o', kernel, *objects])
assert 'NativePathUnusedBaseline' not in run('kernel-symbols', ['/usr/bin/nm', kernel])
audit = repo / 'tools/audit-kernel-instructions.py'
bind(audit)
run('kernel-instruction-audit', ['/usr/bin/python3.8', '-B', audit, '--map', mapfile, kernel])
kernel.chmod(0o444)
assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == value for path, value in inputs.items())
(args.out / 'manifest.json').write_text(json.dumps({'kernel': identity(kernel), 'modules': modules,
    'kernel_fp_policy': 'NativeFpSse2', 'inputs': inputs, 'commands': commands,
    'source_before_after_identical': True,
    'scope': 'current-source native file/typed-remove service and POSIX subset; no inherited qualification lock or guest verdict'}, indent=2) + '\n')
print('NATIVE_PATH_GUEST_BUILD_PASS kernel=1 modules=6 undefined=0 scalar_kernel=true')
