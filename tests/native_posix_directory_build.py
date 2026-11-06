#!/usr/bin/env python3
"""Build the real native file adapters with an injected ABI endpoint for unit tests."""
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
inputs = {}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def bind(path):
    path = path.resolve()
    value = sha(path)
    assert str(path) not in inputs or inputs[str(path)] == value
    inputs[str(path)] = value


def run(name, command):
    result = subprocess.run([str(value) for value in command], cwd=args.out,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (args.out / (name + '.log')).write_text(result.stdout)
    assert result.returncode == 0, name + '\n' + result.stdout
    return result.stdout


def elf(path):
    data = path.read_bytes()
    assert data[:5] == b'\x7fELF\x01' and struct.unpack_from('<H', data, 18)[0] == 3


flags = ['--target=i686-unknown-none-elf', '-ffreestanding', '-fno-builtin', '-nostdinc',
         '-D__GTOS__=1', '-fno-stack-protector', '-fno-pic', '-fno-pie', '-fno-unwind-tables',
         '-fno-asynchronous-unwind-tables', '-fno-exceptions', '-fno-rtti', '-std=c++17',
         '-ffunction-sections', '-fdata-sections', '-Wall', '-Wextra', '-Werror',
         '-I' + str(repo / 'include'), '-I' + str(product / 'include'),
         '-isystem' + str(sdk / 'sdk'), '-isystem' + str(sdk / 'c-sdk'),
         '-isystem' + str(args.clang_bin.parent / 'lib/clang/24/include')]
start = args.out / 'start.s'
start.write_text('''.global _start
.section .text
_start:
  xorl %ebp, %ebp
  andl $-16, %esp
  call DirectoryTestMain
  movl %eax, %ebx
  movl $1, %eax
  int $0x80
  ud2
.section .note.GNU-stack,"",@progbits
''')
bind(start)
run('start', [args.clang_bin / 'clang', '--target=i686-unknown-none-elf', '-c', start,
              '-o', args.out / 'start.o'])
sources = [product / 'posix_file.cpp', product / 'directory.cpp', repo / 'tests/native_posix_directory_test.cpp']
images = []
production = []
for optimization, negative in [(0, False), (2, False), (2, True)]:
    name = 'O' + str(optimization) + ('-bad' if negative else '')
    directory = args.out / name
    directory.mkdir()
    objects = []
    for source in sources:
        bind(source)
        output = directory / (source.stem + '.o')
        defines = ['-DGTOS_POSIX_FILE_TEST=1']
        if negative:
            defines += ['-DGTOS_DIRECTORY_TEST_BAD_EXPECTATION=1']
        run(name + '-' + source.stem, [args.clang_bin / 'clang', *flags, *defines,
            '-O' + str(optimization), '-MD', '-MF', str(output) + '.d', '-c', source, '-o', output])
        elf(output)
        for path in Path(str(output) + '.d').read_text().replace('\\\n', ' ').split(':', 1)[1].split():
            bind(Path(path))
        objects.append(output)
    if not negative:
        for source in sources[:2]:
            output = directory / (source.stem + '.production.o')
            run(name + '-' + source.stem + '-production', [args.clang_bin / 'clang', *flags,
                 '-O' + str(optimization), '-c', source, '-o', output])
            elf(output)
            undefined = run(name + '-' + source.stem + '-symbols', ['/usr/bin/nm', '-u', output])
            assert 'GtosPosixFileTestCall' not in undefined
            production.append({'path': str(output), 'bytes': output.stat().st_size, 'sha256': sha(output)})
    image = directory / 'directory_test.elf'
    run(name + '-link', [args.clang_bin / 'ld.lld', '-m', 'elf_i386', '-static', '-e', '_start',
        '-Ttext=0x40000000', '--gc-sections', '-o', image, args.out / 'start.o', *objects])
    elf(image)
    assert not run(name + '-undefined', ['/usr/bin/nm', '-u', image]).strip()
    image.chmod(0o555)
    images.append({'path': str(image), 'bytes': image.stat().st_size, 'sha256': sha(image),
                   'expected_return': 1 if negative else 0})
assert all(sha(Path(path)) == value for path, value in inputs.items())
(args.out / 'manifest.json').write_text(json.dumps({'images': images, 'production_objects': production,
    'inputs': inputs, 'source_before_after_identical': True,
    'scope': 'real adapter units with injected kernel/allocator dependencies; Linux reporting only, no GTOS guest claim'}, indent=2) + '\n')
print('DIRECTORY_ADAPTER_BUILD_PASS images=3 production_objects=4')
