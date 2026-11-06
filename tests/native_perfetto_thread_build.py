#!/usr/bin/env python3
"""Build the actual GTOS Perfetto thread checker and identity-boundary controls."""
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
source = args.view / 'third_party/perfetto/src/base/thread_checker.cc'
fixture = repo / 'tests/native_perfetto_thread_test.cpp'
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
  call ThreadTestMain
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
for optimization in (0, 2):
    native = args.out / ('policy-O' + str(optimization) + '.o')
    bind(source)
    run(native.stem, [args.clang_bin / 'clang', *flags, '-O' + str(optimization),
        '-MD', '-MF', str(native) + '.d', '-c', source, '-o', native])
    for path in Path(str(native) + '.d').read_text().replace('\\\n', ' ').split(':', 1)[1].split():
        bind(Path(path))
    for mode, define in [('control', None), ('wrong', 'GTOS_THREAD_WRONG_EXPECTATION'), ('invalid', 'GTOS_THREAD_INVALID_OWNER')]:
        name = 'O' + str(optimization) + '-' + mode
        output = args.out / (name + '.o')
        bind(fixture)
        run(name, [args.clang_bin / 'clang', *flags, '-O' + str(optimization),
            *(['-D' + define + '=1'] if define else []), '-c', fixture, '-o', output])
        image = args.out / (name + '.elf')
        run(name + '-link', [args.clang_bin / 'ld.lld', '-m', 'elf_i386', '-static', '-e', '_start',
            '-T', layout, '--gc-sections', '-o', image, args.out / 'start.o', native, output])
        assert not run(name + '-undefined', ['/usr/bin/nm', '-u', image]).strip()
        data = image.read_bytes()
        assert data[:5] == b'\x7fELF\x01' and struct.unpack_from('<H', data, 18)[0] == 3
        phoff = struct.unpack_from('<I', data, 28)[0]
        headers = [struct.unpack_from('<8I', data, phoff + index * 32)
                   for index in range(struct.unpack_from('<H', data, 44)[0])]
        assert any(item[0] == 1 and item[6] == 6 and item[4] > 0 for item in headers)
        image.chmod(0o555)
        images.append({'path': str(image), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                       'mode': mode, 'expected_return': {'control': 0, 'wrong': 1, 'invalid': -4}[mode]})

# Host pthread behavior must still compile with its actual SDK.
host_flags = ['--target=x86_64-unknown-linux-gnu', '--gcc-toolchain=/usr', '-std=c++17', *includes]
run('host-thread-checker', [args.clang_bin / 'clang', *host_flags, '-c', source,
    '-o', args.out / 'host-thread-checker.o'])
assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == value for path, value in inputs.items())
(args.out / 'manifest.json').write_text(json.dumps({'images': images, 'inputs': inputs,
    'source_before_after_identical': True,
    'scope': 'actual Perfetto checker with injected native identity and logging boundaries; Linux reporting; no GTOS guest claim'}, indent=2) + '\n')
print('PERFETTO_THREAD_BUILD_PASS images=6 host_thread_checker_compiles=true')
