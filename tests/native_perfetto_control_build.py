#!/usr/bin/env python3
"""Build the actual GTOS Perfetto signal policy and request-failure controls."""
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
source = args.view / 'third_party/perfetto/src/base/ctrl_c_handler_gtos.cc'
fixture = repo / 'tests/native_perfetto_control_test.cpp'
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
  call ControlTestMain
  movl %eax, %ebx
  movl $1, %eax
  int $0x80
  ud2
.section .note.GNU-stack,"",@progbits
''')
bind(start)
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
    for mode, define in [('control', 'GTOS_CONTROL_NO_REQUEST'), ('request', None), ('null', 'GTOS_CONTROL_NULL_HANDLER')]:
        name = 'O' + str(optimization) + '-' + mode
        output = args.out / (name + '.o')
        bind(fixture)
        run(name, [args.clang_bin / 'clang', *flags, '-O' + str(optimization),
            *(['-D' + define + '=1'] if define else []), '-c', fixture, '-o', output])
        image = args.out / (name + '.elf')
        run(name + '-link', [args.clang_bin / 'ld.lld', '-m', 'elf_i386', '-static', '-e', '_start',
            '-Ttext=0x40000000', '--gc-sections', '-o', image, args.out / 'start.o', native, output])
        assert not run(name + '-undefined', ['/usr/bin/nm', '-u', image]).strip()
        data = image.read_bytes()
        assert data[:5] == b'\x7fELF\x01' and struct.unpack_from('<H', data, 18)[0] == 3
        image.chmod(0o555)
        images.append({'path': str(image), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                       'mode': mode, 'expected_return': 0 if mode == 'control' else -4})

# Host-source compilation only: its POSIX implementation is retained, and the
# native-only implementation must reject accidental host selection.
host_flags = ['--target=x86_64-unknown-linux-gnu', '--gcc-toolchain=/usr', '-std=c++17', *includes]
run('host-original', [args.clang_bin / 'clang', *host_flags, '-c',
    args.view / 'third_party/perfetto/src/base/ctrl_c_handler.cc', '-o', args.out / 'host-original.o'])
run('host-native-refused', [args.clang_bin / 'clang', *host_flags, '-c', source,
    '-o', args.out / 'host-native.o'], False, 'native Ctrl-C policy requires the GTOS platform')
assert not (args.out / 'host-native.o').exists()
assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == value for path, value in inputs.items())
(args.out / 'manifest.json').write_text(json.dumps({'images': images, 'inputs': inputs,
    'source_before_after_identical': True,
    'scope': 'actual native policy with injected logging sink and Linux reporting; host branch compile only; no GTOS guest claim'}, indent=2) + '\n')
print('PERFETTO_CONTROL_BUILD_PASS images=6 host_original_compiles=true host_native_refused=true')
