#!/usr/bin/env python3
"""Build the actual native Perfetto system information and its syscall/environment boundary tests."""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--view', required=True, type=Path)
parser.add_argument('--clang-bin', required=True, type=Path)
parser.add_argument('--generated', required=True, type=Path)
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
args.out.mkdir(parents=True, exist_ok=False)
sdk = repo / 'apps/native_v8_runtime'
full = Path('/mnt/f/GTOS-Chromium/sources/chromium-full/src')
source = args.view / 'third_party/perfetto/src/base/system_info_gtos.cc'
fixture = repo / 'tests/native_perfetto_system_info_test.cpp'
inputs = {}
bound_paths = set()


def bind(path):
    key = str(path)
    if key in bound_paths:
        return
    path = path.resolve()
    if str(path) not in inputs:
        inputs[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    bound_paths.add(key)


def run(name, command, good=True, message=None):
    result = subprocess.run([str(value) for value in command], cwd=args.out,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (args.out / (name + '.log')).write_text(result.stdout)
    assert (result.returncode == 0) == good, name + '\n' + result.stdout
    if message:
        assert message in result.stdout, result.stdout
    return result.stdout


includes = ['-I' + str(args.view / 'third_party/perfetto/include'),
            '-I' + str(args.generated / 'third_party/perfetto/build_config')]
flags = ['--target=i686-unknown-none-elf', '-D__GTOS__=1', '-ffreestanding', '-fno-builtin',
         '-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_NONE',
         '-nostdinc', '-fno-stack-protector', '-fno-pic', '-fno-pie', '-fno-unwind-tables',
         '-fno-asynchronous-unwind-tables', '-fno-exceptions', '-fno-rtti', '-std=c++17',
         '-ffunction-sections', '-fdata-sections', *includes,
         '-isystem' + str(args.view / 'build/config/gtos/include'),
         '-isystem' + str(sdk / 'sdk'), '-isystem' + str(full / 'third_party/libc++/src/include'),
         '-isystem' + str(sdk / 'c-sdk'),
         '-isystem' + str(args.clang_bin.parent / 'lib/clang/24/include')]
start = args.out / 'start.s'
start.write_text('''.global _start
.section .text
_start:
  andl $-16, %esp
  call InfoTestMain
  movl %eax, %ebx
  movl $1, %eax
  int $0x80
  ud2
.section .note.GNU-stack,"",@progbits
''')
bind(start)
layout = args.out / 'test-image.ld'
layout.write_text('''ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
  . = 0x40000000;
  .text : { *(.text*) } :text
  .rodata : { *(.rodata*) } :text
  . = ALIGN(4096);
  .data : { BYTE(0); *(.data*) } :data
  .bss : { *(.bss*) *(COMMON) } :data
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
''')
bind(layout)
run('start', [args.clang_bin / 'clang', '--target=i686-unknown-none-elf', '-c', start,
              '-o', args.out / 'start.o'])
images = []


def dependencies(path):
    for value in Path(str(path) + '.d').read_text().replace('\\\n', ' ').split(':', 1)[1].split():
        bind(Path(value))


for optimization in (0, 2):
    string_source = full / 'third_party/libc++/src/src/string.cpp'
    bind(string_source)
    string_object = args.out / ('libcxx-string-O' + str(optimization) + '.o')
    run(string_object.stem, [args.clang_bin / 'clang', *flags, '-O' + str(optimization),
        '-D_LIBCPP_BUILDING_LIBRARY', '-MD', '-MF', str(string_object) + '.d',
        '-c', string_source, '-o', string_object])
    dependencies(string_object)
    production = args.out / ('info-native-O' + str(optimization) + '.o')
    bind(source)
    run(production.stem, [args.clang_bin / 'clang', *flags, '-O' + str(optimization),
        '-MD', '-MF', str(production) + '.d', '-c', source, '-o', production])
    dependencies(production)
    assert 'GtosPerfettoInfoTestCall' not in run(production.stem + '-symbols', ['/usr/bin/nm', '-u', production])
    implementation = args.out / ('info-test-O' + str(optimization) + '.o')
    run(implementation.stem, [args.clang_bin / 'clang', *flags, '-O' + str(optimization),
        '-DGTOS_PERFETTO_INFO_TEST', '-MD', '-MF', str(implementation) + '.d', '-c', source, '-o', implementation])
    dependencies(implementation)
    for wrong in (False, True):
        name = 'O' + str(optimization) + ('-wrong' if wrong else '-control')
        output = args.out / (name + '.o')
        bind(fixture)
        run(name, [args.clang_bin / 'clang', *flags, '-O' + str(optimization),
            *(['-DGTOS_INFO_WRONG_EXPECTATION'] if wrong else []),
            '-MD', '-MF', str(output) + '.d', '-c', fixture, '-o', output])
        dependencies(output)
        image = args.out / (name + '.elf')
        run(name + '-link', [args.clang_bin / 'ld.lld', '-m', 'elf_i386', '-static', '-e', '_start',
            '-T', layout, '--gc-sections', '-o', image, args.out / 'start.o', string_object, implementation, output])
        assert not run(name + '-undefined', ['/usr/bin/nm', '-u', image]).strip()
        data = image.read_bytes()
        assert data[:5] == b'\x7fELF\x01' and struct.unpack_from('<H', data, 18)[0] == 3
        image.chmod(0o555)
        images.append({'path': str(image), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                       'mode': 'wrong' if wrong else 'control', 'expected_return': 1 if wrong else 0})

host = full / 'third_party/perfetto/src/base/android_utils.cc'
bind(host)
run('host-system-info', [args.clang_bin / 'clang', '--target=x86_64-unknown-linux-gnu', '--gcc-toolchain=/usr',
    '-std=c++17', *includes, '-c', host, '-o', args.out / 'host-system-info.o'])
assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == value for path, value in inputs.items())
(args.out / 'manifest.json').write_text(json.dumps({'images': images, 'inputs': inputs,
    'source_before_after_identical': True,
    'scope': 'actual native Perfetto system info and libc++ containers; injected syscall/environment/allocator/logging boundaries and Linux reporting; no guest claim'}, indent=2) + '\n')
print('PERFETTO_SYSTEM_INFO_BUILD_PASS images=4 native_objects=2 host_source_compiles=true')
