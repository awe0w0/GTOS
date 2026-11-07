#!/usr/bin/env python3
"""Build actual Perfetto task queues with injected native syscall boundaries."""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--view', required=True, type=Path)
p.add_argument('--clang-bin', required=True, type=Path)
p.add_argument('--generated', required=True, type=Path)
p.add_argument('--out', required=True, type=Path)
args = p.parse_args()
repo = Path(__file__).resolve().parents[1]
full = Path('/mnt/f/GTOS-Chromium/sources/chromium-full/src')
sdk = repo / 'apps/native_v8_runtime'
args.out.mkdir(parents=True, exist_ok=False)
inputs, seen = {}, set()


def bind(path):
    if str(path) in seen:
        return
    seen.add(str(path))
    path = path.resolve()
    if str(path) not in inputs:
        inputs[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()


def run(name, command):
    r = subprocess.run([str(x) for x in command], cwd=args.out,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (args.out / (name + '.log')).write_text(r.stdout)
    assert r.returncode == 0, name + '\n' + r.stdout
    return r.stdout


def deps(obj):
    for path in Path(str(obj) + '.d').read_text().replace('\\\n', ' ').split(':', 1)[1].split():
        bind(Path(path))


includes = ['-I' + str(args.view / 'third_party/perfetto/include'),
            '-I' + str(args.generated / 'third_party/perfetto/build_config')]
flags = ['--target=i686-unknown-none-elf', '-D__GTOS__=1', '-ffreestanding', '-fno-builtin',
         '-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_NONE', '-nostdinc',
         '-fno-stack-protector', '-fno-pic', '-fno-pie', '-fno-unwind-tables',
         '-fno-asynchronous-unwind-tables', '-fno-exceptions', '-fno-rtti', '-std=c++17',
         '-ffunction-sections', '-fdata-sections', *includes,
         '-I' + str(repo / 'include'), '-I' + str(repo / 'apps/native_libxml_file/include'),
         '-isystem' + str(args.view / 'build/config/gtos/include'),
         '-isystem' + str(sdk / 'sdk'), '-isystem' + str(full / 'third_party/libc++/src/include'),
         '-isystem' + str(sdk / 'c-sdk'),
         '-isystem' + str(args.clang_bin.parent / 'lib/clang/24/include')]
start = args.out / 'start.s'
start.write_text('''.global _start
.section .text
_start:
  andl $-16, %esp
  call RunnerTestMain
  movl %eax, %ebx
  movl $1, %eax
  int $0x80
  ud2
.section .note.GNU-stack,"",@progbits
''')
layout = args.out / 'test.ld'
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
bind(start)
bind(layout)
run('start', [args.clang_bin / 'clang', '--target=i686-unknown-none-elf', '-c', start, '-o', args.out / 'start.o'])
builtins = Path('/mnt/f/GTOS-Chromium/sources/chromium-gtos/src/out/gtos-scalar-integer-x86-v2/obj/tools/gtos/compiler_runtime/libscalar_integer.a')
assert hashlib.sha256(builtins.read_bytes()).hexdigest() == 'f2340f1a4fd61b7472983e04fda2a0fc86108e649f614a25d043bff5fdc2f528'
bind(builtins)
images = []
for optimization in (0, 2):
    objects = []
    sources = [(name, args.view / ('third_party/perfetto/src/base/' + name + '.cc'), [])
               for name in ['unix_task_runner', 'lock_free_task_runner', 'thread_checker', 'virtual_destructors', 'utils']]
    sources += [('event', args.view / 'third_party/perfetto/src/base/event_fd_gtos.cc', ['-DGTOS_PERFETTO_EVENT_TEST']),
                ('wait', args.view / 'third_party/perfetto/src/base/native_wait_gtos.cc', ['-DGTOS_PERFETTO_WAIT_TEST']),
                ('mutex', full / 'third_party/libc++/src/src/mutex.cpp', ['-D_LIBCPP_BUILDING_LIBRARY']),
                ('mutex-destructor', full / 'third_party/libc++/src/src/mutex_destructor.cpp', ['-D_LIBCPP_BUILDING_LIBRARY']),
                ('hash', full / 'third_party/libc++/src/src/hash.cpp', ['-D_LIBCPP_BUILDING_LIBRARY']),
                ('ceilf', full / 'third_party/llvm-libc/src/src/math/generic/ceilf.cpp',
                 ['-I' + str(full / 'third_party/llvm-libc/src'), '-DLIBC_NAMESPACE=__llvm_libc_gtos_content',
                  '-DLIBC_COPT_PUBLIC_PACKAGING', '-DLIBC_ERRNO_MODE=6']),
                ('bytes', repo / 'apps/native_libxml_file/link_closure/min_closure.cpp', [])]
    for name, source, extra in sources:
        bind(source)
        obj = args.out / (name + '-O' + str(optimization) + '.o')
        run(obj.stem, [args.clang_bin / 'clang', *flags, '-O' + str(optimization), *extra,
                      '-MD', '-MF', str(obj) + '.d', '-c', source, '-o', obj])
        deps(obj)
        objects.append(obj)
    production = args.out / ('wait-native-O' + str(optimization) + '.o')
    run(production.stem, [args.clang_bin / 'clang', *flags, '-O' + str(optimization),
                         '-MD', '-MF', str(production) + '.d', '-c',
                         args.view / 'third_party/perfetto/src/base/native_wait_gtos.cc', '-o', production])
    deps(production)
    assert 'GtosPerfettoWaitTestCall' not in run(production.stem + '-symbols', ['/usr/bin/nm', '-u', production])
    for name, case, wrong, status in [('control', 0, False, 0), ('wrong', 0, True, 1)] + [
            ('invalid' + str(i), i, False, -4) for i in range(1, 12)]:
        label = 'O' + str(optimization) + '-' + name
        source = repo / 'tests/native_perfetto_runner_test.cpp'
        bind(source)
        obj = args.out / (label + '.o')
        run(label, [args.clang_bin / 'clang', *flags, '-O' + str(optimization), '-DGTOS_RUNNER_CASE=' + str(case),
                    *(['-DGTOS_RUNNER_WRONG_EXPECTATION'] if wrong else []),
                    '-MD', '-MF', str(obj) + '.d', '-c', source, '-o', obj])
        deps(obj)
        image = args.out / (label + '.elf')
        run(label + '-link', [args.clang_bin / 'ld.lld', '-m', 'elf_i386', '-static', '-e', '_start',
                             '-T', layout, '--gc-sections', '-o', image, args.out / 'start.o', *objects, obj, builtins])
        assert not run(label + '-undefined', ['/usr/bin/nm', '-u', image]).strip()
        data = image.read_bytes()
        assert data[:5] == b'\x7fELF\x01' and struct.unpack_from('<H', data, 18)[0] == 3
        image.chmod(0o555)
        images.append({'path': str(image), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                       'mode': name, 'expected_return': status})

# Compile the original branches with the host ABI as a regression. Native wait
# code is neither injected into nor used to replace the host implementations.
for name in ['unix_task_runner', 'lock_free_task_runner', 'time', 'utils']:
    run('host-' + name, [args.clang_bin / 'clang', '--target=x86_64-unknown-linux-gnu', '--gcc-toolchain=/usr',
        '-std=c++17', *includes, '-c', args.view / ('third_party/perfetto/src/base/' + name + '.cc'),
        '-o', args.out / ('host-' + name + '.o')])
assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == sha for path, sha in inputs.items())
(args.out / 'manifest.json').write_text(json.dumps({'images': images, 'inputs': inputs,
    'source_before_after_identical': True,
    'scope': 'actual native Perfetto runners/wait backend and pinned libc++ mutex/container code; injected event/clock/thread/allocation/log boundaries; no guest or multi-thread-race claim'}, indent=2) + '\n')
print('PERFETTO_RUNNER_BUILD_PASS images=26 production_wait_objects=2 host_sources=4')
