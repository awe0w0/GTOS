#!/usr/bin/env python3
"""Build real native Perfetto backends and the current-source CPL3 observers."""
import argparse
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--view', required=True, type=Path)
parser.add_argument('--clang-bin', required=True, type=Path)
parser.add_argument('--generated', required=True, type=Path)
parser.add_argument('--out', required=True, type=Path)
parser.add_argument('--component', choices=['memory', 'cpu', 'event', 'wait', 'exit', 'partition', 'boringssl',
                                            'base_identity', 'base_event', 'base_shared', 'base_tls', 'loopback',
                                            'native_thread', 'webrtc_thread', 'vp9', 'crt_arguments',
                                            'skia_semaphore', 'expat', 'address_text', 'net_protocol',
                                            'content_capacity', 'ffmpeg_webm', 'v8_platform',
                                            'platform_libraries'],
                    default='memory')
parser.add_argument('--payload', type=Path)
parser.add_argument('--payload-record', type=Path)
parser.add_argument('--provider-root', type=Path)
parser.add_argument('--ffmpeg-config-root', type=Path)
parser.add_argument('--media-provider-root', type=Path)
args = parser.parse_args()
if args.component == 'crt_arguments':
    assert args.payload is not None and args.payload_record is not None
else:
    assert args.payload is None and args.payload_record is None
assert (args.provider_root is not None) == (args.component in ('expat', 'ffmpeg_webm'))
assert (args.ffmpeg_config_root is not None) == (args.component == 'ffmpeg_webm')
assert (args.media_provider_root is not None) == (args.component == 'ffmpeg_webm')
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
    'exit': ('native_exit_record', None, 'exit.cpp', 'native_content_exit_kernel.cpp', 'ContentExitUnusedBaseline', 3),
    'partition': ('native_memory_record', None, 'partition_pages.cpp',
                  'native_partition_pages_kernel.cpp', 'PartitionPagesUnusedBaseline', 3),
    'boringssl': ('native_memory_record', None, 'boringssl.cpp',
                  'native_content_boringssl_kernel.cpp', 'ContentBoringSslUnusedBaseline', 3),
    'base_identity': ('native_memory_record', None, 'base_identity.cpp',
                      'native_content_identity_kernel.cpp', 'ContentIdentityUnusedBaseline', 1),
    'loopback': ('native_memory_record', None, 'loopback.cpp',
                 'native_content_loopback_kernel.cpp', 'ContentLoopbackUnusedBaseline', 2),
    'base_event': ('native_memory_record', None, 'base_event.cpp',
                   'native_content_base_event_kernel.cpp', 'ContentBaseEventUnusedBaseline', 1),
    'base_shared': ('native_memory_record', None, 'base_shared_memory.cpp',
                    'native_content_shared_memory_kernel.cpp', 'ContentSharedMemoryUnusedBaseline', 2),
    'base_tls': ('native_memory_record', None, 'base_tls.cpp',
                 'native_content_identity_kernel.cpp', 'ContentIdentityUnusedBaseline', 1),
    'native_thread': ('native_memory_record', None, 'native_thread.cpp',
                      'native_content_identity_kernel.cpp', 'ContentIdentityUnusedBaseline', 1),
    'skia_semaphore': ('native_memory_record', None, 'skia_semaphore.cpp',
                       'native_content_identity_kernel.cpp', 'ContentIdentityUnusedBaseline', 1),
    'expat': ('native_memory_record', None, 'expat.cpp',
              'native_content_identity_kernel.cpp', 'ContentIdentityUnusedBaseline', 1),
    'address_text': ('native_memory_record', None, 'address_text.cpp',
                     'native_content_identity_kernel.cpp', 'ContentIdentityUnusedBaseline', 1),
    'net_protocol': ('native_memory_record', None, 'net_protocol.cpp',
                     'native_content_identity_kernel.cpp', 'ContentIdentityUnusedBaseline', 1),
    'content_capacity': ('native_memory_record', None, 'content_capacity.cpp',
                         'native_content_identity_kernel.cpp', 'ContentIdentityUnusedBaseline', 1),
    'webrtc_thread': ('native_memory_record', None, 'webrtc_thread.cpp',
                      'native_content_identity_kernel.cpp', 'ContentIdentityUnusedBaseline', 1),
    'vp9': ('native_memory_record', None, 'vp9.cpp',
            'native_content_identity_kernel.cpp', 'ContentIdentityUnusedBaseline', 1),
    'ffmpeg_webm': ('native_memory_record', None, 'ffmpeg_webm.cpp',
                    'native_content_identity_kernel.cpp', 'ContentIdentityUnusedBaseline', 1),
    'v8_platform': ('native_memory_record', None, 'v8_platform.cpp',
                   'native_content_identity_kernel.cpp', 'ContentIdentityUnusedBaseline', 1),
    'platform_libraries': ('native_memory_record', None, 'platform_libraries.cpp',
                           'native_content_identity_kernel.cpp', 'ContentIdentityUnusedBaseline', 1),
    'crt_arguments': (None, None, None,
                      'gtos_content_arguments_kernel.cpp', 'ContentArgumentsUnusedBaseline', 0),
}[args.component]
record_name, backend, consumer, observer, unused_entry, mode_count = component
family = args.component.upper()
inputs, commands = {}, []


def bind(path):
    path = path.resolve()
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    assert str(path) not in inputs or inputs[str(path)] == digest
    inputs[str(path)] = digest


bind(Path(__file__))


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
    flags += ['-I' + str(args.view), '-I' + str(args.view / 'third_party/abseil-cpp'),
              '-idirafter', str(repo / 'include')]
if args.component in ('base_identity', 'base_event', 'base_shared', 'base_tls', 'native_thread',
                      'webrtc_thread', 'vp9', 'skia_semaphore', 'address_text', 'net_protocol',
                      'v8_platform', 'platform_libraries'):
    v8 = repo / 'apps/native_v8_integration/overlay'
    runtime = v8 / 'tools/gtos_isolate_diagnostic/runtime'
    partition = Path('base/allocator/partition_allocator/src')
    flags += ['-std=c++23', '-DOFFICIAL_BUILD', '-DNDEBUG', '-I' + str(args.view), '-I' + str(args.generated),
              '-I' + str(args.view / partition), '-I' + str(args.generated / partition),
              '-I' + str(args.view / 'third_party/abseil-cpp'),
              '-I' + str(args.view / 'third_party/boringssl/src/include'),
              '-I' + str(v8), '-I' + str(runtime / 'sdk')]
if args.component in ('v8_platform', 'platform_libraries'):
    flags.insert(0, '-I' + str(args.view / 'v8'))
    flags.append('-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_NONE')
if args.component == 'v8_platform':
    flags += ['-DGTOS_CONTENT_V8_PLATFORM=1', '-femulated-tls']
if args.component == 'platform_libraries':
    cpuinfo = args.view / 'third_party/cpuinfo/src'
    angle = args.view / 'third_party/angle'
    dawn = args.view / 'third_party/dawn'
    flags += ['-I' + str(path) for path in (
        cpuinfo / 'include', cpuinfo / 'src', cpuinfo / 'deps/clog/include',
        angle / 'src', angle / 'include', dawn)]
    flags += ['-DCPUINFO_INTERNAL=', '-DCPUINFO_PRIVATE=', '-DCPUINFO_LOG_LEVEL=0']
if args.component == 'base_event':
    # Isolate the event and diagnostic paths from the trace-session runtime.
    # The real trace-enabled object is compiled separately by Content Ninja.
    disabled = args.out / 'disable_event_tracing.h'
    disabled.write_text('#include "base/trace_event/trace_event.h"\n'
                        '#undef TRACE_EVENT_CATEGORY_ENABLED\n'
                        '#define TRACE_EVENT_CATEGORY_ENABLED(...) false\n'
                        '#undef TRACE_EVENT_INSTANT\n'
                        '#define TRACE_EVENT_INSTANT(...) ((void)0)\n')
    bind(disabled)
    flags += ['-I' + str(args.generated / 'third_party/perfetto')]
if args.component in ('base_tls', 'native_thread', 'skia_semaphore'):
    strings = repo / 'apps/native_c_string/llvm-libc'
    flags.append('-I' + str(strings))
if args.component == 'webrtc_thread':
    flags.append('-I' + str(args.view / 'third_party/webrtc'))
if args.component == 'skia_semaphore':
    formal_rule = args.generated.parent / 'obj/skia/skia_core_and_effects.ninja'
    bind(formal_rule)
    hardening = re.findall(r'-D_LIBCPP_HARDENING_MODE=\w+', formal_rule.read_text())
    assert len(hardening) == 1, 'missing exact formal libc++ hardening mode'
    flags += [hardening[0], '-I' + str(args.view / 'third_party/skia')]
if args.component in ('address_text', 'net_protocol'):
    runtime = args.view / 'v8/tools/gtos_isolate_diagnostic/runtime'
    flags += ['-I' + str(args.view / 'v8')]
if args.component == 'expat':
    llvm_libc = args.view / 'third_party/llvm-libc/src'
    environment = args.view / 'tools/gtos_native_sdk/environment'
    flags += ['-std=c++23', '-I' + str(args.view), '-I' + str(environment),
              '-I' + str(llvm_libc), '-DXML_STATIC']
ffmpeg_opus = False
if args.component in ('vp9', 'ffmpeg_webm'):
    llvm_libc = full / 'third_party/llvm-libc/src'
    flags += ['-I' + str(args.view / 'third_party/libvpx/source/libvpx'),
              '-I' + str(llvm_libc)]
    webm = args.component == 'ffmpeg_webm'
    fixture = full / ('media/test/data/bear-vp9.webm' if webm else 'media/test/data/bear-vp9.ivf')
    bind(fixture)
    data = fixture.read_bytes()
    if webm:
        assert len(data) == 67504 and data[:4] == b'\x1aE\xdf\xa3'
        assert hashlib.sha256(data).hexdigest() == '7cd68c55f4bb47a2df22a85fac9e8b942cbbba24286c037048131944eb0a09eb'
        components = args.provider_root / 'build/config_components.h'
        bind(components)
        opus_define = re.search(r'^#define CONFIG_LIBOPUS_DECODER ([01])$', components.read_text(), re.M)
        assert opus_define is not None
        ffmpeg_opus = opus_define[1] == '1'
        flags += ['-std=c++23', '-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_NONE',
                  '-I' + str(product / 'sdk/ffmpeg'),
                  '-I' + str(args.view), '-I' + str(args.view / 'v8'),
                  '-I' + str(args.view / 'third_party/ffmpeg'),
                  '-I' + str(args.provider_root / 'build')]
        fixture_symbol, fixture_size = 'gtos_webm_fixture', len(data)
    else:
        assert hashlib.sha256(data).hexdigest() == '02e3eb33651a3e54a07a4f8644e59b065f7dc48257787710b193b82728594f59'
        assert data[:4] == b'DKIF' and data[8:12] == b'VP90'
        assert struct.unpack_from('<HH', data, 12) == (320, 240)
        assert struct.unpack_from('<IQ', data, 32) == (3792, 0)
        assert struct.unpack_from('<IQ', data, 3836) == (64, 33)
        fixture_symbol, fixture_size = 'gtos_vp9_fixture', 3912
    fixture_source = args.out / 'fixture.S'
    fixture_source.write_text(f'.section .rodata.vp9,"a"\n.globl {fixture_symbol}\n'
                              f'{fixture_symbol}:\n.incbin "{fixture}", 0, {fixture_size}\n')
    if webm:
        reference = full / 'media/test/data/bear-vp9.ivf'
        bind(reference)
        reference_bytes = reference.read_bytes()
        assert hashlib.sha256(reference_bytes).hexdigest() == '02e3eb33651a3e54a07a4f8644e59b065f7dc48257787710b193b82728594f59'
        assert struct.unpack_from('<IQ', reference_bytes, 32) == (3792, 0)
        assert struct.unpack_from('<IQ', reference_bytes, 3836) == (64, 33)
        with fixture_source.open('a') as stream:
            stream.write('.globl gtos_vp9_reference\ngtos_vp9_reference:\n'
                         f'.incbin "{reference}", 0, 3912\n')
        if ffmpeg_opus:
            opus = full / 'media/test/data/bear-opus.ogg'
            bind(opus)
            opus_bytes = opus.read_bytes()
            assert len(opus_bytes) == 22397 and opus_bytes[:4] == b'OggS'
            assert hashlib.sha256(opus_bytes).hexdigest() == (
                'c074407b7126060972b5e9eb4081e04e35c88857d7125151cbb9e5d9a1191dae')
            with fixture_source.open('a') as stream:
                stream.write('.globl gtos_opus_fixture\ngtos_opus_fixture:\n'
                             f'.incbin "{opus}"\n')
    fixture_object = args.out / 'fixture.o'
    run('assemble-vp9-fixture', [args.clang_bin / 'clang', '--target=i686-unknown-none-elf',
                                '-c', fixture_source, '-o', fixture_object])
    jump_source = args.view / 'build/config/gtos/runtime/setjmp_i386.S'
    bind(jump_source)
    jump_object = args.out / 'setjmp.o'
    run('assemble-setjmp', [args.clang_bin / 'clang', '--target=i686-unknown-none-elf',
                            '-c', jump_source, '-o', jump_object])
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
modules = []
if args.component == 'crt_arguments':
    bind(args.payload)
    bind(args.payload_record)
    record = json.loads(args.payload_record.read_bytes())
    assert record['consumer'] == 'arguments' and record['undefined'] == 0
    data = args.payload.read_bytes()
    assert 0 < len(data) <= 65536 and data[:5] == b'\x7fELF\x01'
    assert len(data) == record['bytes'] and hashlib.sha256(data).hexdigest() == record['sha256']
    image = args.out / 'crt_arguments.elf'
    with image.open('xb') as output:
        output.write(data)
    image.chmod(0o444)
    modules.append({'optimization': 2, 'mode': 0, 'negative': False, **identity(image)})
else:
    layout_source = repo / 'apps/native_vm_probe/linker.ld'
    bind(layout_source)
    layout = args.out / 'module.ld'
    layout_text = layout_source.read_text().replace('native_vm_record', record_name)
    record_address = '40020000'
    image_limit = 65536
    if ffmpeg_opus:
        record_address = '40300000'
        image_limit = 4 * 1024 * 1024
        layout_text = (layout_text.replace('0x40020000', '0x' + record_address)
                       .replace('0x20000', '0x300000')
                       .replace('<= 4096, "VM probe data exceeds one page"',
                                '<= 0x100000, "VM probe data exceeds full media budget"'))
    elif args.component in ('vp9', 'ffmpeg_webm', 'webrtc_thread', 'skia_semaphore',
                          'expat', 'address_text', 'net_protocol', 'v8_platform',
                          'platform_libraries'):
        record_address = '40200000'
        image_limit = 2 * 1024 * 1024
        layout_text = (layout_text.replace('0x40020000', '0x' + record_address)
                       .replace('0x20000', '0x200000').replace('<= 4096,', '<= 65536,'))
    if args.component == 'content_capacity':
        layout_text = layout_text.replace('*(.bss*)', '*(.bss)').replace(
            '    /DISCARD/',
            '    .content_bss (NOLOAD) : { *(.bss.content_capacity) } :data\n    /DISCARD/')
    layout.write_text(layout_text)
    bind(layout)
    start = repo / 'apps/native_libxml_file/probe/exit-start.s'
    bind(start)
    run('start', [args.clang_bin / 'clang', '--target=i686-unknown-none-elf', '-c', start,
                  '-o', args.out / 'start.o'])
for optimization in (() if args.component == 'crt_arguments' else (0, 2)):
    directory = args.out / ('O' + str(optimization))
    directory.mkdir()
    objects = []
    native_sources = [('bytes', repo / 'apps/native_libxml_file/link_closure/min_closure.cpp')]
    if args.component == 'address_text':
        native_sources = [('address', product / 'runtime/address_text.cc'),
                          ('url-address', args.view / 'url/url_canon_ip.cc'),
                          ('url-output', args.view / 'url/url_canon.cc'),
                          ('url-parse', args.view / 'url/third_party/mozilla/url_parse.cc'),
                          ('thread-context', runtime / 'thread-context.cc')]
    if args.component == 'net_protocol':
        native_sources = [('cookie', args.view / 'net/cookies/cookie_util.cc'),
                          ('parsed-cookie', args.view / 'net/cookies/parsed_cookie.cc'),
                          ('dns-name', args.view / 'net/dns/dns_names_util.cc'),
                          ('net-url', args.view / 'net/base/url_util.cc'),
                          ('thread-context', runtime / 'thread-context.cc')]
    if args.component == 'vp9':
        native_sources = [('assert', product / 'runtime/assert.cc'),
                          ('codec-once', args.view / 'build/config/gtos/runtime/codec_once.cc'),
                          ('thread-core', runtime / 'thread-core.cc'),
                          ('qsort', llvm_libc / 'src/stdlib/qsort.cpp')]
    elif args.component == 'ffmpeg_webm':
        native_sources = [('codec-once', args.view / 'build/config/gtos/runtime/codec_once.cc'),
                          ('qsort', llvm_libc / 'src/stdlib/qsort.cpp'),
                          ('hyperbolic', product / 'sdk/ffmpeg/hyperbolic.cc'),
                          ('tanh', product / 'sdk/tanh.cc'),
                          ('ieee754', args.view / 'v8/src/base/ieee754.cc')]
    elif args.component == 'v8_platform':
        platform = args.view / 'v8/src/base/platform'
        native_runtime = args.view / 'v8/tools/gtos_isolate_diagnostic/runtime'
        native_sources = [('v8-once', args.view / 'v8/src/base/once.cc'),
                          ('v8-sys-info', args.view / 'v8/src/base/sys-info.cc'),
                          ('v8-page-allocator', args.view / 'v8/src/base/page-allocator.cc'),
                          ('v8-native-os', platform / 'platform-gtos.cc'),
                          ('v8-native-stack-trace', args.view / 'v8/src/base/debug/stack_trace_gtos.cc'),
                          ('v8-time', platform / 'time.cc'),
                          ('clock-bridge', platform / 'gtos-clock-bridge.cc'),
                          ('native-thread', args.view / 'tools/gtos_native_sdk/native-thread.cc'),
                          ('thread-context', native_runtime / 'thread-context.cc'),
                          ('thread-core', native_runtime / 'thread-core.cc'),
                          ('gtos-semaphore', native_runtime / 'gtos-semaphore.cc'),
                          ('assert', product / 'runtime/assert.cc')]
    elif args.component == 'platform_libraries':
        cpu_sources = (
            'api.c', 'cache.c', 'init.c', 'log.c', 'x86/gtos/init.c',
            'x86/info.c', 'x86/init.c', 'x86/isa.c', 'x86/name.c',
            'x86/topology.c', 'x86/uarch.c', 'x86/vendor.c',
            'x86/cache/descriptor.c', 'x86/cache/deterministic.c', 'x86/cache/init.c',
        )
        native_sources = [('cpuinfo-' + name.replace('/', '-'), cpuinfo / 'src' / name)
                          for name in cpu_sources]
        native_runtime = args.view / 'v8/tools/gtos_isolate_diagnostic/runtime'
        native_sources += [
            ('angle-strings', angle / 'src/common/string_utils.cpp'),
            ('dawn-sleep', dawn / 'src/dawn/utils/SystemUtils.cpp'),
            ('codec-once', args.view / 'build/config/gtos/runtime/codec_once.cc'),
            ('native-thread', args.view / 'tools/gtos_native_sdk/native-thread.cc'),
            ('thread-context', native_runtime / 'thread-context.cc'),
            ('thread-core', native_runtime / 'thread-core.cc'),
            ('clock-bridge', args.view / 'v8/src/base/platform/gtos-clock-bridge.cc'),
            ('assert', product / 'runtime/assert.cc'),
        ]
    elif args.component == 'webrtc_thread':
        rtc_sources = args.view / 'third_party/webrtc/rtc_base'
        native_sources = [('native-thread', args.view / 'tools/gtos_native_sdk/native-thread.cc'),
                          ('thread-core', runtime / 'thread-core.cc'),
                          ('assert', product / 'runtime/assert.cc'),
                          # Standalone WebRTC diagnostics use its actual stderr
                          # implementation without pulling in Chromium logging.
                          ('rtc-checks', rtc_sources / 'checks.cc'),
                          ('rtc-utc', rtc_sources / 'time_utils.cc')]
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
    if args.component == 'base_event':
        native_sources += [('base-event', args.view / 'base/synchronization/waitable_event.cc'),
                           ('base-event-gtos', args.view / 'base/synchronization/waitable_event_gtos.cc'),
                           ('base-raw-log', args.view / 'base/logging.cc'),
                           ('heap', runtime / 'heap.cc'), ('operators', runtime / 'operators.cc')]
    if args.component == 'base_shared':
        native_sources += [('base-shared', args.view / 'base/memory/platform_shared_memory_gtos.cc'),
                           ('mojo-platform-handle', args.view / 'mojo/public/cpp/platform/platform_handle.cc'),
                           ('mojo-handle-utils', args.view / 'mojo/core/platform_handle_utils.cc'),
                           ('scoped-file', args.view / 'base/files/scoped_file.cc'),
                           ('posix-file', repo / 'apps/native_libxml_file/posix_file.cpp'),
                           ('base-metrics', args.view / 'base/process/process_metrics_gtos.cc'),
                           ('metrics-common', args.view / 'base/process/process_metrics.cc'),
                           ('heap', runtime / 'heap.cc'), ('operators', runtime / 'operators.cc'),
                           ('abort', runtime / 'abort.cc')]
    if args.component in ('base_tls', 'native_thread', 'skia_semaphore'):
        native_sources = (
            [('base-tls', args.view / 'base/threading/thread_local_storage_gtos.cc')]
            if args.component == 'base_tls' else
            [('native-thread', args.view / 'tools/gtos_native_sdk/native-thread.cc'),
             ('thread-core', runtime / 'thread-core.cc')])
        native_sources += [(name, strings / ('src/string/' + name + '.cpp')) for name in
                           ('memcpy', 'memset', 'memmove')]
        native_sources += [(name, runtime / (name + '.cc')) for name in
                           ('thread-context', 'thread-exit', 'emutls', 'heap', 'operators', 'abort')]
    if args.component == 'skia_semaphore':
        for name in ('SkSemaphore', 'SkThreadID'):
            native_sources.append((name, args.view / ('third_party/skia/src/core/' + name + '.cpp')))
    if args.component == 'expat':
        native_sources += [('assert', product / 'runtime/assert.cc'),
                           ('environment', environment / 'environment.cc'),
                           ('environ_internal', llvm_libc / 'src/stdlib/environ_internal.cpp'),
                           ('getenv', llvm_libc / 'src/stdlib/getenv.cpp')]
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
        packaging = (['-DLIBC_NAMESPACE=__llvm_libc_cr', '-DLIBC_COPT_PUBLIC_PACKAGING=1']
                     if name in ('strchr', 'memcpy', 'memset', 'memmove', 'qsort',
                                 'environment', 'environ_internal', 'getenv') else [])
        if name == 'base-event':
            packaging += ['-include', str(disabled)]
        compile_flags = flags
        if source.suffix == '.c':
            compile_flags = [flag for flag in flags if not flag.startswith('-std=')] + ['-std=c11']
        run(directory.name + '-' + name, [args.clang_bin / 'clang', *compile_flags, *packaging, '-O' + str(optimization),
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
        linked = image.with_suffix('.unstripped.elf') if args.component in (
            'partition', 'boringssl', 'base_event', 'base_shared', 'base_tls',
            'native_thread', 'webrtc_thread', 'vp9', 'skia_semaphore', 'expat', 'address_text',
            'net_protocol', 'ffmpeg_webm', 'v8_platform', 'platform_libraries') else image
        extra = [asm_obj] if args.component == 'boringssl' else []
        if args.component == 'skia_semaphore':
            archive = args.view.parent / 'runtime-archive-ra03/libgtos_content_runtime.a'
            bind(archive)
            extra.append(archive)
        if args.component in ('address_text', 'net_protocol', 'v8_platform', 'platform_libraries'):
            archive = args.view.parent / 'runtime-archive-ra03/libgtos_content_runtime.a'
            bind(archive)
            extra.append(archive)
        if args.component == 'expat':
            archive = args.view.parent / 'runtime-archive-ra03/libgtos_content_runtime.a'
            manifest = args.provider_root / 'identity.json'
            bind(manifest)
            published = json.loads(manifest.read_text())['compiled']
            providers = []
            for provider_name in ('xmlparse', 'xmlrole', 'xmltok'):
                path = args.provider_root / (provider_name + '.o')
                record = published[provider_name]['object']
                assert not path.is_symlink() and not path.stat().st_mode & 0o222
                assert path.stat().st_size == record['bytes']
                assert hashlib.sha256(path.read_bytes()).hexdigest() == record['sha256']
                providers.append(path)
            for path in [*providers, archive]:
                bind(path)
            extra += [*providers, archive]
        if args.component == 'webrtc_thread':
            build = args.generated.parent
            rtc_objects = build / 'obj/third_party/webrtc/rtc_base'
            archive = args.view.parent / 'runtime-archive-ra02/libgtos_content_runtime.a'
            assert hashlib.sha256(archive.read_bytes()).hexdigest() == (
                'd775c338d68de3da28c69cc8768684738b1a428d1f0048612cb9e8f92771c7c6')
            providers = [rtc_objects / name for name in (
                'criticalsection/recursive_critical_section.o',
                'platform_thread/platform_thread.o',
                'platform_thread_types/platform_thread_types.o')]
            providers += [build / 'obj/v8/v8_libbase' / (name + '.o')
                          for name in ('gtos-clock-bridge', 'thread-context', 'gtos-semaphore')]
            for path in [*providers, archive]:
                bind(path)
            extra += [*providers, archive]
        if args.component == 'vp9':
            build = args.generated.parent
            archive = args.view.parent / 'runtime-archive-ra02/libgtos_content_runtime.a'
            codec = build / 'obj/third_party/libvpx/libvpx.a'
            assert hashlib.sha256(codec.read_bytes()).hexdigest() == (
                '785805632bcc1b09a0a3eaa15b20b9f710fcf66405bbc4c8582202b8b4c301ae')
            assert hashlib.sha256(archive.read_bytes()).hexdigest() == (
                'd775c338d68de3da28c69cc8768684738b1a428d1f0048612cb9e8f92771c7c6')
            providers = [build / 'obj/v8/v8_libbase' / (name + '.o')
                         for name in ('gtos-clock-bridge', 'thread-context', 'gtos-semaphore')]
            for path in [codec, archive, *providers]:
                bind(path)
            extra += [fixture_object, jump_object, codec, *providers, archive]
        if args.component == 'ffmpeg_webm':
            archive = args.view.parent / 'runtime-archive-ra03/libgtos_content_runtime.a'
            media = args.media_provider_root
            media_record = media / 'identity.json'
            bind(media_record)
            providers_record = json.loads(media_record.read_text())
            provider_names = ['libvpx.a', 'gtos-clock-bridge.o', 'gtos-semaphore.o']
            if ffmpeg_opus:
                provider_names.append('libopus.a')
            for provider_name in provider_names:
                path = media / provider_name
                actual = identity(path)
                assert not path.is_symlink() and not path.stat().st_mode & 0o222
                assert all(actual[key] == providers_record[provider_name][key]
                           for key in ('sha256', 'bytes'))
                bind(path)
            codec = media / 'libvpx.a'
            record_path = args.provider_root / 'build-identity.json'
            configuration_path = args.ffmpeg_config_root / 'identity.json'
            for path in (record_path, configuration_path, archive, codec):
                bind(path)
            built = json.loads(record_path.read_text())
            configured = json.loads(configuration_path.read_text())
            assert built['configuration'] == {
                k: v for k, v in identity(configuration_path).items() if k != 'path'}
            for product_name, expected in configured['outputs'].items():
                path = args.provider_root / 'build' / product_name
                assert {k: v for k, v in identity(path).items() if k != 'path'} == expected
                bind(path)
            libraries = []
            for family_name in ('avformat', 'avcodec', 'avutil'):
                library_name = 'lib' + family_name
                relative = library_name + '/' + library_name + '.a'
                path = args.provider_root / 'build' / relative
                actual = identity(path)
                assert all(actual[key] == built['archives'][relative][key]
                           for key in ('sha256', 'bytes'))
                bind(path)
                libraries.append(path)
            providers = []
            for path_name, expected in sorted(configured['inputs'].items()):
                path = Path(path_name)
                if path.parent == args.ffmpeg_config_root and path.suffix == '.o':
                    assert {k: v for k, v in identity(path).items() if k != 'path'} == expected
                    bind(path)
                    providers.append(path)
            assert {p.name for p in providers} >= {
                'assert.o', 'pthread.o', 'native-thread.o', 'thread-core.o'}
            engine = [media / (name + '.o')
                      for name in ('gtos-clock-bridge', 'gtos-semaphore')]
            for path in engine:
                bind(path)
            opus_archive = [media / 'libopus.a'] if ffmpeg_opus else []
            extra += [fixture_object, jump_object, codec, *libraries, *opus_archive,
                      *providers, *engine, archive]
        if cpu or args.component in ('wait', 'partition', 'webrtc_thread', 'vp9', 'expat', 'address_text',
                                     'net_protocol', 'ffmpeg_webm', 'v8_platform', 'platform_libraries'):
            builtins = Path('/mnt/f/GTOS-Chromium/sources/chromium-gtos/src/out/gtos-scalar-integer-x86-v2/obj/tools/gtos/compiler_runtime/libscalar_integer.a')
            assert hashlib.sha256(builtins.read_bytes()).hexdigest() == 'f2340f1a4fd61b7472983e04fda2a0fc86108e649f614a25d043bff5fdc2f528'
            bind(builtins)
            extra.append(builtins)
        run(directory.name + '-' + name + '-link', [args.clang_bin / 'ld.lld', '-m', 'elf_i386',
            '-static', '-e', '_start', '-T', layout, '--gc-sections',
            *(['--error-limit=0'] if args.component == 'ffmpeg_webm' else []), '-o', linked,
            args.out / 'start.o', *objects, output, *extra])
        assert not run(directory.name + '-' + name + '-undefined', ['/usr/bin/nm', '-u', linked]).strip()
        symbols = run(directory.name + '-' + name + '-symbols', ['/usr/bin/nm', '-n', linked])
        assert record_address + ' D ' + record_name in symbols
        if linked != image:
            run(directory.name + '-' + name + '-strip', ['/usr/bin/objcopy', '--strip-all', linked, image])
            assert loaded_image(linked.read_bytes()) == loaded_image(image.read_bytes())
        data = image.read_bytes()
        assert data[:5] == b'\x7fELF\x01' and struct.unpack_from('<H', data, 18)[0] == 3 and len(data) <= image_limit
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
if args.component in ('event', 'wait', 'base_event'):
    sources.append('src/process/native_events.cpp')
if args.component == 'base_shared':
    sources.append('src/process/native_shared_memory.cpp')
if args.component == 'loopback':
    sources.append('src/process/native_loopback.cpp')
assembly = ['tests/native_process_loader.s', 'tests/native_process_user.s',
            'src/process/native_fp.s', 'src/hardwarecommunication/interruptstubs.s']
bind(repo / 'tools/kernel-cxxflags')
kernel_flags = (repo / 'tools/kernel-cxxflags').read_text().split()
if args.component == 'base_tls':
    kernel_flags.append('-DGTOS_CONTENT_TLS_PROBE')
if args.component == 'native_thread':
    kernel_flags.append('-DGTOS_CONTENT_THREAD_PROBE')
if args.component == 'skia_semaphore':
    kernel_flags.append('-DGTOS_CONTENT_SKIA_SEMAPHORE_PROBE')
if args.component == 'expat':
    kernel_flags.append('-DGTOS_CONTENT_EXPAT_PROBE')
if args.component == 'address_text':
    kernel_flags.append('-DGTOS_CONTENT_ADDRESS_TEXT_PROBE')
if args.component == 'net_protocol':
    kernel_flags.append('-DGTOS_CONTENT_NET_PROTOCOL_PROBE')
if args.component == 'content_capacity':
    kernel_flags.append('-DGTOS_CONTENT_CAPACITY_PROBE')
if args.component == 'webrtc_thread':
    kernel_flags.append('-DGTOS_CONTENT_WEBRTC_THREAD_PROBE')
if args.component == 'vp9':
    kernel_flags.append('-DGTOS_CONTENT_VP9_PROBE')
if args.component == 'ffmpeg_webm':
    kernel_flags.append('-DGTOS_CONTENT_FFMPEG_WEBM_PROBE')
    if ffmpeg_opus:
        kernel_flags.append('-DGTOS_CONTENT_FFMPEG_OPUS_PROBE')
if args.component == 'v8_platform':
    kernel_flags.append('-DGTOS_CONTENT_V8_PLATFORM_PROBE')
if args.component == 'platform_libraries':
    kernel_flags.append('-DGTOS_CONTENT_PLATFORM_LIBRARIES_PROBE')
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
prefix = 'CONTENT_' if args.component in (
    'exit', 'partition', 'boringssl', 'base_identity', 'base_event', 'base_shared', 'loopback',
    'base_tls', 'native_thread', 'webrtc_thread', 'vp9', 'crt_arguments',
    'skia_semaphore', 'expat', 'address_text', 'net_protocol', 'content_capacity',
    'ffmpeg_webm', 'v8_platform', 'platform_libraries') else 'PERFETTO_'
print(prefix + args.component.upper() + '_GUEST_BUILD_PASS kernel=1 modules=' +
      str(len(modules)) + ' undefined=0 scalar_kernel=true')
