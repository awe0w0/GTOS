#!/usr/bin/env python3
"""Build the actual GTOS Perfetto clock adapter against actual LLVM UTC calendar code."""
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
source = args.view / 'third_party/perfetto/include/perfetto/base/time.h'
fixture = repo / 'tests/native_perfetto_time_test.cpp'
inputs = {}


def bind(path):
    path = path.resolve()
    value = hashlib.sha256(path.read_bytes()).hexdigest()
    assert str(path) not in inputs or inputs[str(path)] == value
    inputs[str(path)] = value


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
  call TimeTestMain
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
libc = full / 'third_party/llvm-libc/src'
builtins = Path('/mnt/f/GTOS-Chromium/sources/chromium-gtos/src/out/gtos-scalar-integer-x86-v2/obj/tools/gtos/compiler_runtime/libscalar_integer.a')
assert hashlib.sha256(builtins.read_bytes()).hexdigest() == 'f2340f1a4fd61b7472983e04fda2a0fc86108e649f614a25d043bff5fdc2f528'
bind(source)
bind(builtins)
calendar = {'mktime.cpp': 'c09a98d989139c428ec1f74ceb9e68d95d46bc30da2908b1ddb9b704e30ad100',
            'time_utils.cpp': '677e4d5750a36ea85e58d6540a3517e7e88c3fd28edd6fcd3c4422eb03245b15'}

def dependencies(path):
    for value in Path(str(path) + '.d').read_text().replace('\\\n', ' ').split(':', 1)[1].split():
        bind(Path(value))

for optimization in (0, 2):
    objects = []
    for name, expected in calendar.items():
        native_source = libc / 'src/time' / name
        assert hashlib.sha256(native_source.read_bytes()).hexdigest() == expected
        bind(native_source)
        output = args.out / ('calendar-O' + str(optimization) + '-' + Path(name).stem + '.o')
        run(output.stem, [args.clang_bin / 'clang', *flags, '-O' + str(optimization),
            '-I' + str(libc), '-DLIBC_NAMESPACE=__llvm_libc_gtos_content',
            '-DLIBC_COPT_PUBLIC_PACKAGING', '-DLIBC_ERRNO_MODE=6',
            '-MD', '-MF', str(output) + '.d', '-c', native_source, '-o', output])
        dependencies(output)
        objects.append(output)
    cases = [('control', 0, False, 0), ('wrong', 0, True, 1)] + [
        ('invalid' + str(index), index, False, -4) for index in range(1, 7)]
    for mode, value, wrong, expected in cases:
        name = 'O' + str(optimization) + '-' + mode
        output = args.out / (name + '.o')
        bind(fixture)
        run(name, [args.clang_bin / 'clang', *flags, '-O' + str(optimization),
            '-DGTOS_TIME_CASE=' + str(value), *(['-DGTOS_TIME_WRONG_EXPECTATION'] if wrong else []),
            '-MD', '-MF', str(output) + '.d', '-c', fixture, '-o', output])
        dependencies(output)
        image = args.out / (name + '.elf')
        run(name + '-link', [args.clang_bin / 'ld.lld', '-m', 'elf_i386', '-static', '-e', '_start',
            '-T', layout, '--gc-sections', '-o', image, args.out / 'start.o', *objects, output, builtins])
        assert not run(name + '-undefined', ['/usr/bin/nm', '-u', image]).strip()
        data = image.read_bytes()
        assert data[:5] == b'\x7fELF\x01' and struct.unpack_from('<H', data, 18)[0] == 3
        image.chmod(0o555)
        images.append({'path': str(image), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                       'mode': mode, 'expected_return': expected})

host_fixture = args.out / 'host-header.cpp'
host_fixture.write_text('#include "perfetto/base/time.h"\nlong long f() { return perfetto::base::GetWallTimeNs().count(); }\n')
run('host-time-header', [args.clang_bin / 'clang', '--target=x86_64-unknown-linux-gnu', '--gcc-toolchain=/usr',
    '-std=c++17', *includes, '-c', host_fixture, '-o', args.out / 'host-header.o'])
assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == value for path, value in inputs.items())
(args.out / 'manifest.json').write_text(json.dumps({'images': images, 'inputs': inputs,
    'source_before_after_identical': True,
    'scope': 'actual checked Perfetto clock conversion plus pinned LLVM UTC calendar; injected clock/logging boundaries and Linux reporting; no guest claim'}, indent=2) + '\n')
print('PERFETTO_TIME_BUILD_PASS images=16 host_time_header_compiles=true')
