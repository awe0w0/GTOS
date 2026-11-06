#!/usr/bin/env python3
"""Build the actual GTOS Perfetto native VM-backed page memory with an injected kernel endpoint."""
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
source = args.view / 'third_party/perfetto/src/base/paged_memory_gtos.cc'
fixture = repo / 'tests/native_perfetto_memory_test.cpp'
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
  call MemoryTestMain
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
production = []
for optimization in (0, 2):
    native = args.out / ('native-O' + str(optimization) + '.o')
    bind(source)
    run(native.stem, [args.clang_bin / 'clang', *flags, '-DGTOS_PERFETTO_VM_TEST',
        '-O' + str(optimization), '-MD', '-MF', str(native) + '.d', '-c', source, '-o', native])
    for path in Path(str(native) + '.d').read_text().replace('\\\n', ' ').split(':', 1)[1].split():
        bind(Path(path))
    default = args.out / ('production-O' + str(optimization) + '.o')
    run(default.stem, [args.clang_bin / 'clang', *flags, '-O' + str(optimization),
        '-c', source, '-o', default])
    assert 'GtosPerfettoVmTestCall' not in run(default.stem + '-symbols', ['/usr/bin/nm', '-u', default])
    production.append({'path': str(default), 'sha256': hashlib.sha256(default.read_bytes()).hexdigest()})
    cases = [('control', 0, False, 0), ('wrong', 0, True, 1)] + [
        ('invalid' + str(index), index, False, -4) for index in range(1, 7)]
    for mode, value, wrong, expected in cases:
        name = 'O' + str(optimization) + '-' + mode
        output = args.out / (name + '.o')
        bind(fixture)
        run(name, [args.clang_bin / 'clang', *flags, '-O' + str(optimization),
            '-DGTOS_MEMORY_CASE=' + str(value), *(['-DGTOS_MEMORY_WRONG_EXPECTATION'] if wrong else []),
            '-c', fixture, '-o', output])
        image = args.out / (name + '.elf')
        run(name + '-link', [args.clang_bin / 'ld.lld', '-m', 'elf_i386', '-static', '-e', '_start',
            '-T', layout, '--gc-sections', '-o', image, args.out / 'start.o', native, output])
        assert not run(name + '-undefined', ['/usr/bin/nm', '-u', image]).strip()
        data = image.read_bytes()
        assert data[:5] == b'\x7fELF\x01' and struct.unpack_from('<H', data, 18)[0] == 3
        image.chmod(0o555)
        images.append({'path': str(image), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                       'mode': mode, 'expected_return': expected})

host_flags = ['--target=x86_64-unknown-linux-gnu', '--gcc-toolchain=/usr', '-std=c++17', *includes]
run('host-original', [args.clang_bin / 'clang', *host_flags, '-c',
    args.view / 'third_party/perfetto/src/base/paged_memory.cc', '-o', args.out / 'host-original.o'])
assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == value for path, value in inputs.items())
(args.out / 'manifest.json').write_text(json.dumps({'images': images, 'production_objects': production, 'inputs': inputs,
    'source_before_after_identical': True,
    'scope': 'actual VM-backed page owner with injected kernel and logging boundaries; Linux reporting; no native guest protection claim'}, indent=2) + '\n')
print('PERFETTO_MEMORY_BUILD_PASS images=16 production_objects=2 host_original_compiles=true')
