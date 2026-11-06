#!/usr/bin/env python3
"""Build the actual GTOS Perfetto non-terminal console behavior in Chromium and standalone profiles."""
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
source = args.view / 'third_party/perfetto/src/base/progress_reporter.cc'
fixture = repo / 'tests/native_perfetto_console_test.cpp'
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
  call ConsoleTestMain
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
origin = args.generated / 'third_party/perfetto/build_config/perfetto_build_flags.h'
bind(origin)
configuration = origin.read_text()
needle = '#define PERFETTO_BUILDFLAG_DEFINE_PERFETTO_CHROMIUM_BUILD() (1)'
assert configuration.count(needle) == 1
profiles = {}
for chromium in (True, False):
    profile = args.out / ('chromium' if chromium else 'standalone')
    profile.mkdir()
    selected = profile / 'perfetto_build_flags.h'
    selected.write_text(configuration if chromium else configuration.replace(needle, needle[:-2] + '0)'))
    bind(selected)
    profiles[chromium] = profile
for optimization, chromium, negative in [(0, True, False), (0, False, False),
                                          (2, True, False), (2, False, False), (2, False, True)]:
    name = 'O' + str(optimization) + ('-chromium' if chromium else '-standalone') + ('-bad' if negative else '')
    native = args.out / (name + '-reporter.o')
    bind(source)
    selected_flags = ['-I' + str(profiles[chromium]), *flags]
    run(native.stem, [args.clang_bin / 'clang', *selected_flags, '-O' + str(optimization),
        '-MD', '-MF', str(native) + '.d', '-c', source, '-o', native])
    for path in Path(str(native) + '.d').read_text().replace('\\\n', ' ').split(':', 1)[1].split():
        bind(Path(path))
    bind(fixture)
    output = args.out / (name + '.o')
    run(name, [args.clang_bin / 'clang', *selected_flags, '-O' + str(optimization),
        *(['-DGTOS_CONSOLE_WRONG_EXPECTATION'] if negative else []), '-c', fixture, '-o', output])
    image = args.out / (name + '.elf')
    run(name + '-link', [args.clang_bin / 'ld.lld', '-m', 'elf_i386', '-static', '-e', '_start',
        '-T', layout, '--gc-sections', '-o', image, args.out / 'start.o', native, output])
    assert not run(name + '-undefined', ['/usr/bin/nm', '-u', image]).strip()
    data = image.read_bytes()
    assert data[:5] == b'\x7fELF\x01' and struct.unpack_from('<H', data, 18)[0] == 3
    image.chmod(0o555)
    images.append({'path': str(image), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                   'expected_return': 1 if negative else 0})
host_flags = ['--target=x86_64-unknown-linux-gnu', '--gcc-toolchain=/usr', '-std=c++17', *includes]
run('host-original', [args.clang_bin / 'clang', *host_flags, '-c', source, '-o', args.out / 'host-original.o'])
assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == value for path, value in inputs.items())
(args.out / 'manifest.json').write_text(json.dumps({'images': images, 'inputs': inputs,
    'source_before_after_identical': True,
    'scope': 'actual progress reporter terminal/color decisions with injected environment; two build profiles; Linux reporting; no guest claim'}, indent=2) + '\n')
print('PERFETTO_CONSOLE_BUILD_PASS images=5 host_original_compiles=true')
