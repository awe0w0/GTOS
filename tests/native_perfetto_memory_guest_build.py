#!/usr/bin/env python3
"""Build real native Perfetto backends and the current-source CPL3 observers."""
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
parser.add_argument('--component', choices=['memory', 'cpu', 'event', 'wait', 'exit', 'partition', 'boringssl', 'base_identity'],
                    default='memory')
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
args.out.mkdir(parents=True, exist_ok=False)
sdk = repo / 'apps/native_v8_runtime'
full = Path('/mnt/f/GTOS-Chromium/sources/chromium-full/src')
product = repo / 'apps/native_content_build/gtos-platform'
cpu = args.component == 'cpu'
component = {
    'memory': ('native_memory_record', 'paged_memory_gtos.cc', 'memory.cpp', 'native_perfetto_memory_kernel.cpp', 'PerfettoMemoryUnusedBaseline', 5),
    'cpu': ('native_cpu_record', 'cpu_time_gtos.cc', 'cpu_time.cpp', 'native_perfetto_cpu_time_kernel.cpp', 'PerfettoCpuUnusedBaseline', 3),
    'event': ('native_event_record', 'event_fd_gtos.cc', 'event.cpp', 'native_perfetto_event_kernel.cpp', 'PerfettoEventUnusedBaseline', 3),
    'wait': ('native_wait_record', 'native_wait_gtos.cc', 'wait.cpp', 'native_perfetto_wait_kernel.cpp', 'PerfettoWaitUnusedBaseline', 3),
    'exit': ('native_exit_record', None, 'exit.cpp', 'native_content_exit_kernel.cpp', 'ContentExitUnusedBaseline', 2),
    'partition': ('native_memory_record', None, 'partition_pages.cpp',
                  'native_partition_pages_kernel.cpp', 'PartitionPagesUnusedBaseline', 2),
    'boringssl': ('native_memory_record', None, 'boringssl.cpp',
                  'native_content_boringssl_kernel.cpp', 'ContentBoringSslUnusedBaseline', 3),
    'base_identity': ('native_memory_record', None, 'base_identity.cpp',
                      'native_content_identity_kernel.cpp', 'ContentIdentityUnusedBaseline', 1),
}[args.component]
record_name, backend, consumer, observer, unused_entry, mode_count = component
family = args.component.upper()
inputs, commands = {}, []


def bind(path):
    path = path.resolve()
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    assert str(path) not in inputs or inputs[str(path)] == digest
    inputs[str(path)] = digest


def run(name, command):
    command = [str(value) for value in command]
    commands.append(command)
    result = subprocess.run(command, cwd=repo, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    (args.out / (name + '.log')).write_text(result.stdout)
    assert result.returncode == 0, name + '\n' + result.stdout
    return result.stdout


def dependencies(path):
    for value in Path(str(path) + '.d').read_text().replace('\\\n', ' ').split(':', 1)[1].split():
        bind(repo / value)


def identity(path):
    return {'path': str(path), 'bytes': path.stat().st_size,
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def loaded_image(data):
    assert data[:6] == b'\x7fELF\x01\x01'
    entry, offset = struct.unpack_from('<II', data, 24)
    size, count = struct.unpack_from('<HH', data, 42)
    segments = []
    for index in range(count):
        kind, start, va, pa, filesz, memsz, flags, align = struct.unpack_from(
            '<8I', data, offset + size * index)
        if kind == 1:
            segments.append((va, pa, filesz, memsz, flags, align, data[start:start + filesz]))
    assert segments
    return entry, segments


flags = ['--target=i686-unknown-none-elf', '-D__GTOS__=1', '-ffreestanding', '-fno-builtin',
         '-mno-mmx', '-msse2', '-mfpmath=sse', '-mno-sse3', '-mno-ssse3', '-mno-sse4', '-mno-avx',
         '-nostdinc', '-fno-stack-protector', '-fno-pic', '-fno-pie', '-fno-unwind-tables',
         '-fno-asynchronous-unwind-tables', '-fno-exceptions', '-fno-rtti', '-std=c++17',
         '-ffunction-sections', '-fdata-sections', '-I' + str(product / 'probe'),
         '-I' + str(args.view / 'third_party/perfetto/include'),
         '-I' + str(args.generated / 'third_party/perfetto/build_config'),
         '-isystem' + str(args.view / 'build/config/gtos/include'),
         '-isystem' + str(repo / 'apps/native_libxml_file/include'),
         '-isystem' + str(sdk / 'sdk'), '-isystem' + str(full / 'third_party/libc++/src/include'),
         '-isystem' + str(sdk / 'c-sdk'),
         '-isystem' + str(args.clang_bin.parent / 'lib/clang/24/include')]
if args.component == 'wait':
    flags += ['-I' + str(sdk / 'include')]
if args.component == 'exit':
    flags += ['-I' + str(args.view), '-I' + str(args.view / 'third_party/abseil-cpp')]
if args.component == 'base_identity':
    v8 = repo / 'apps/native_v8_integration/overlay'
    runtime = v8 / 'tools/gtos_isolate_diagnostic/runtime'
    partition = Path('base/allocator/partition_allocator/src')
    flags += ['-std=c++23', '-DOFFICIAL_BUILD', '-DNDEBUG', '-I' + str(args.view), '-I' + str(args.generated),
              '-I' + str(args.view / partition), '-I' + str(args.generated / partition),
              '-I' + str(args.view / 'third_party/abseil-cpp'),
              '-I' + str(args.view / 'third_party/boringssl/src/include'),
              '-I' + str(v8), '-I' + str(runtime / 'sdk')]
if args.component == 'partition':
    partition = Path('base/allocator/partition_allocator/src')
    pa = args.view / partition / 'partition_alloc'
    v8 = repo / 'apps/native_v8_integration/overlay'
    clock = repo / 'apps/native_c_clock/runtime'
    strings = repo / 'apps/native_c_string/llvm-libc'
    flags += ['-std=c++23', '-I' + str(args.view),
              '-I' + str(args.view / partition), '-I' + str(args.generated / partition),
              '-I' + str(v8), '-I' + str(clock), '-I' + str(strings)]
if args.component == 'boringssl':
    bssl = args.view / 'third_party/boringssl/src'
    md5_probe = repo / 'apps/native_boringssl/probe-md5'
    bssl_include = '-I' + str(bssl / 'include')
    flags += ['-std=c++23', '-DBORINGSSL_IMPLEMENTATION', '-DOPENSSL_SMALL',
              bssl_include, '-I' + str(args.view), '-I' + str(md5_probe)]
    asm = bssl / 'gen/crypto/md5-586-linux.S'
    asm_obj = args.out / 'md5-586-linux.o'
    bind(asm)
    run('assemble-md5', [args.clang_bin / 'clang', '--target=i686-unknown-none-elf',
                         '-nostdinc', '-D__GTOS__=1', bssl_include,
                         '-MD', '-MF', str(asm_obj) + '.d', '-c', asm, '-o', asm_obj])
    dependencies(asm_obj)
    assert ' T md5_block_asm_data_order' in run('md5-asm-symbols', ['/usr/bin/nm', asm_obj])
layout_source = repo / 'apps/native_vm_probe/linker.ld'
bind(layout_source)
layout = args.out / 'module.ld'
layout.write_text(layout_source.read_text().replace('native_vm_record', record_name))
bind(layout)
start = repo / 'apps/native_libxml_file/probe/exit-start.s'
bind(start)
run('start', [args.clang_bin / 'clang', '--target=i686-unknown-none-elf', '-c', start,
              '-o', args.out / 'start.o'])
modules = []
for optimization in (0, 2):
    directory = args.out / ('O' + str(optimization))
    directory.mkdir()
    objects = []
    native_sources = [('bytes', repo / 'apps/native_libxml_file/link_closure/min_closure.cpp')]
    if backend is not None:
        native_sources.insert(0, ('backend', args.view / ('third_party/perfetto/src/base/' + backend)))
    if args.component == 'exit':
        native_sources.append(('failure-signal', args.view /
                               'third_party/abseil-cpp/absl/debugging/failure_signal_handler.cc'))
    if args.component == 'base_identity':
        native_sources += [('base-process', args.view / 'base/process/process_handle.cc'),
                           ('base-thread', args.view / 'base/threading/platform_thread_gtos.cc'),
                           ('base-lock', args.view / 'base/synchronization/lock_impl_gtos.cc'),
                           ('thread-core', runtime / 'thread-core.cc')]
    if args.component == 'wait':
        native_sources += [('events', args.view / 'third_party/perfetto/src/base/event_fd_gtos.cc'),
                           ('clock', repo / 'apps/native_c_clock/runtime/clock-gettime.cc')]
    if args.component == 'partition':
        names = ['page_allocator.cc', 'spinning_mutex.cc', 'stack/stack.cc', 'address_space_randomization.cc',
                 'random.cc', 'partition_alloc_base/rand_util.cc',
                 'partition_alloc_base/rand_util_gtos.cc',
                 'partition_alloc_base/time/time.cc', 'partition_alloc_base/time/time_now_gtos.cc',
                 'partition_alloc_base/threading/platform_thread_gtos.cc',
                 'partition_alloc_base/check.cc', 'partition_alloc_base/debug/alias.cc',
                 'partition_alloc_base/log_message.cc', 'partition_alloc_base/logging.cc',
                 'partition_alloc_base/strings/cstring_builder.cc',
                 'partition_alloc_base/strings/safe_sprintf.cc',
                 'partition_alloc_base/strings/string_util.cc']
        native_sources += [(Path(name).stem, pa / name) for name in names]
        runtime = v8 / 'tools/gtos_isolate_diagnostic/runtime'
        native_sources += [('clock', clock / 'clock-gettime.cc'),
                           ('sleep', runtime / 'nanosleep.cc'),
                           ('heap', runtime / 'heap.cc'),
                           ('operators', runtime / 'operators.cc'),
                           ('strchr', strings / 'src/string/strchr.cpp')]
    if args.component == 'boringssl':
        runtime = repo / 'apps/native_v8_integration/overlay/tools/gtos_isolate_diagnostic/runtime'
        native_sources += [('md5-adapter', md5_probe / 'adapter.cc'),
                           ('md5', bssl / 'crypto/md5/md5.cc'),
                           ('thread-gtos', bssl / 'crypto/thread_gtos.cc'),
                           ('entropy-gtos', bssl / 'crypto/rand/gtos.cc'),
                           ('assert', product / 'runtime/assert.cc'),
                           ('abort', runtime / 'abort.cc'),
                           ('heap', runtime / 'heap.cc')]
    for name, source in native_sources:
        bind(source)
        output = directory / (name + '.o')
        packaging = ['-DLIBC_NAMESPACE=__llvm_libc_cr', '-DLIBC_COPT_PUBLIC_PACKAGING=1'] if name == 'strchr' else []
        run(directory.name + '-' + name, [args.clang_bin / 'clang', *flags, *packaging, '-O' + str(optimization),
            '-MD', '-MF', str(output) + '.d', '-c', source, '-o', output])
        dependencies(output)
        symbols = run(directory.name + '-' + name + '-symbols', ['/usr/bin/nm', '-u', output])
        assert all(value not in symbols for value in ['GtosPerfettoVmTestCall', 'GtosPerfettoCpuTimeTestCall', 'GtosPerfettoEventTestCall', 'GtosPerfettoWaitTestCall'])
        objects.append(output)
    source = product / ('probe/' + consumer)
    bind(source)
    for mode, negative in [(value, False) for value in range(mode_count)] + [(0, True)]:
        name = 'mode' + str(mode) + ('-bad' if negative else '')
        output = directory / (name + '.o')
        run(directory.name + '-' + name, [args.clang_bin / 'clang', *flags, '-O' + str(optimization),
            '-DGTOS_' + family + '_GUEST_MODE=' + str(mode),
            *(['-DGTOS_' + family + '_GUEST_WRONG_EXPECTATION'] if negative else []),
            '-MD', '-MF', str(output) + '.d', '-c', source, '-o', output])
        dependencies(output)
        image = directory / (name + '.elf')
        linked = image.with_suffix('.unstripped.elf') if args.component in ('partition', 'boringssl') else image
        extra = [asm_obj] if args.component == 'boringssl' else []
        if cpu or args.component in ('wait', 'partition'):
            builtins = Path('/mnt/f/GTOS-Chromium/sources/chromium-gtos/src/out/gtos-scalar-integer-x86-v2/obj/tools/gtos/compiler_runtime/libscalar_integer.a')
            assert hashlib.sha256(builtins.read_bytes()).hexdigest() == 'f2340f1a4fd61b7472983e04fda2a0fc86108e649f614a25d043bff5fdc2f528'
            bind(builtins)
            extra.append(builtins)
        run(directory.name + '-' + name + '-link', [args.clang_bin / 'ld.lld', '-m', 'elf_i386',
            '-static', '-e', '_start', '-T', layout, '--gc-sections', '-o', linked,
            args.out / 'start.o', *objects, output, *extra])
        assert not run(directory.name + '-' + name + '-undefined', ['/usr/bin/nm', '-u', linked]).strip()
        symbols = run(directory.name + '-' + name + '-symbols', ['/usr/bin/nm', '-n', linked])
        assert '40020000 D ' + record_name in symbols
        if linked != image:
            run(directory.name + '-' + name + '-strip', ['/usr/bin/objcopy', '--strip-all', linked, image])
            assert loaded_image(linked.read_bytes()) == loaded_image(image.read_bytes())
        data = image.read_bytes()
        assert data[:5] == b'\x7fELF\x01' and struct.unpack_from('<H', data, 18)[0] == 3 and len(data) <= 65536
        image.chmod(0o444)
        modules.append({'optimization': optimization, 'mode': mode, 'negative': negative, **identity(image)})

# The existing runtime, GDT, physical allocator, int80 and page-fault path all
# come from the current worktree. There is no replacement memory policy here.
sources = ['src/gdt.cpp', 'src/multitasking.cpp', 'src/syscalls.cpp',
           'src/hardwarecommunication/interrupts.cpp', 'src/hardwarecommunication/port.cpp',
           'src/process/native_runtime.cpp', 'src/process/native_realtime.cpp', 'src/process/resources.cpp',
           'src/process/native_surface.cpp', 'src/process/native_windows.cpp', 'src/process/native_fp.cpp',
           'src/process/elf32.cpp', 'src/memory/process_address_space.cpp', 'src/memory/paging.cpp',
           'src/memory/physical.cpp', 'src/memory/bootstrap.cpp',
           'tests/i386_pure_virtual_runtime.cpp',
           'tests/' + observer]
if args.component in ('event', 'wait'):
    sources.append('src/process/native_events.cpp')
assembly = ['tests/native_process_loader.s', 'tests/native_process_user.s',
            'src/process/native_fp.s', 'src/hardwarecommunication/interruptstubs.s']
bind(repo / 'tools/kernel-cxxflags')
kernel_flags = (repo / 'tools/kernel-cxxflags').read_text().split()
directory = args.out / 'kernel'
directory.mkdir()
objects = []
for source in sources:
    path = repo / source
    bind(path)
    output = directory / (path.stem + '.o')
    run('kernel-' + path.stem, ['/usr/bin/g++', '-m32', '-std=c++11', '-O2', '-ffreestanding',
        '-nostdlib', '-fno-builtin', '-fno-exceptions', '-fno-rtti', '-fno-stack-protector',
        '-fno-pie', '-fno-threadsafe-statics', '-fno-use-cxa-atexit', '-fno-asynchronous-unwind-tables',
        '-ffunction-sections', '-fdata-sections', '-Iinclude', '-Wno-write-strings',
        '-Wall', '-Wextra', '-Werror', *kernel_flags, '-MD', '-MF', str(output) + '.d', '-c', path, '-o', output])
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
run('kernel-link', ['/usr/bin/ld', '-melf_i386', '--gc-sections', '-T', linker,
                   '-Map', mapfile, '-o', kernel, *objects])
symbols = run('kernel-symbols', ['/usr/bin/nm', kernel])
assert unused_entry not in symbols
audit = repo / 'tools/audit-kernel-instructions.py'
bind(audit)
run('kernel-instruction-audit', ['/usr/bin/python3.8', '-B', audit, '--map', mapfile, kernel])
kernel.chmod(0o444)
assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest() == value for path, value in inputs.items())
(args.out / 'manifest.json').write_text(json.dumps({'kernel': identity(kernel), 'modules': modules,
    'user_profile': 'GTOS IA32 SSE2, matching native Content', 'kernel_fp_policy': 'NativeFpSse2', 'component': args.component,
    'inputs': inputs, 'commands': commands, 'source_before_after_identical': True,
    'scope': 'actual native ' + args.component + ' backend, current-source observer kernel; build only, no guest verdict'}, indent=2) + '\n')
prefix = 'CONTENT_' if args.component in ('exit', 'partition', 'boringssl', 'base_identity') else 'PERFETTO_'
print(prefix + args.component.upper() + '_GUEST_BUILD_PASS kernel=1 modules=' +
      str(len(modules)) + ' undefined=0 scalar_kernel=true')
