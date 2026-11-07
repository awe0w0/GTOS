#!/usr/bin/env python3
"""Execute compiler-level contracts for the real Content ABI headers."""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--view', required=True, type=Path)
parser.add_argument('--clang', required=True)
parser.add_argument('--libcxx', required=True)
parser.add_argument('--generated', required=True, type=Path)
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
args.out.mkdir(parents=True, exist_ok=False)
old_sdk = repo / 'apps/native_v8_runtime'
native = args.view / 'build/config/gtos/include'
headers = [*native.rglob('*'), old_sdk / 'sdk/__external_threading']
headers = [path for path in headers if path.is_file()]
before = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in headers}
flags = ['--target=i686-unknown-none-elf', '-ffreestanding', '-nostdinc', '-D__GTOS__=1',
         '-isystem' + str(native), '-isystem' + str(repo / 'apps/native_libxml_file/include'),
         '-isystem' + str(old_sdk / 'sdk'), '-isystem' + args.libcxx,
         '-isystem' + str(old_sdk / 'c-sdk'),
         '-isystem' + str(Path(args.clang).parents[1] / 'lib/clang/24/include')]
results = []


def compile_case(name, source, options, good=True, diagnostic=None, suffix='.cc'):
    path = args.out / (name + suffix)
    path.write_text(source)
    output = args.out / (name + '.o')
    result = subprocess.run([args.clang, *options, '-c', str(path), '-o', str(output)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (args.out / (name + '.log')).write_text(result.stdout)
    assert (result.returncode == 0) == good, name + '\n' + result.stdout
    if diagnostic:
        assert diagnostic in result.stdout, result.stdout
    if good:
        data = output.read_bytes()
        assert data[:4] == b'\x7fELF' and struct.unpack_from('<H', data, 16)[0] == 1
        if '--target=i686-unknown-none-elf' in options:
            assert data[4] == 1 and struct.unpack_from('<H', data, 18)[0] == 3
    else:
        assert not output.exists(), name
    results.append({'case': name, 'returncode': result.returncode, 'expected_success': good})
    print('PASS', name, flush=True)


source = '''#include <mutex>
#include <condition_variable>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
static_assert(sizeof(std::__libcpp_mutex_t) == 8);
static_assert(sizeof(std::__libcpp_condvar_t) == 8);
static_assert(sizeof(std::__libcpp_thread_t) == 4);
static_assert(sizeof(std::__libcpp_tls_key) == 4);
static_assert(sizeof(uid_t) == 4 && sizeof(mode_t) == 4);
static_assert(sizeof(void*) == 4);
static_assert(sizeof(timeval) == 16 && __builtin_offsetof(timeval, tv_usec) == 8);
void UseNativeMutex(std::mutex& mutex) { mutex.lock(); mutex.unlock(); }
const char* NativeFormat() { return "%" PRId64 " %" PRIuPTR; }
'''
cxx = [*flags, '-x', 'c++', '-std=c++23', '-fno-exceptions', '-fno-rtti']
compile_case('native-cxx', source, cxx)
compile_case('wrong-pointer-width', '#include <__external_threading>\n', [*cxx, '--target=x86_64-unknown-none-elf'],
             False, 'native GTOS IA32 ABI')
compile_case('timeval-host-refusal', '#include <sys/time.h>\n',
             [*cxx, '-D__linux__=1'], False, 'native GTOS IA32 ABI')
compile_case('conflicting-host-os', '#include <__external_threading>\n', [*cxx, '-D__linux__=1'],
             False, 'native GTOS IA32 ABI')
compile_case('real-layout-error', source.replace('sizeof(std::__libcpp_mutex_t) == 8',
                                               'sizeof(std::__libcpp_mutex_t) == 4'),
             cxx, False, 'static assertion failed')
compile_case('native-c', '''#include <inttypes.h>
#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>
_Static_assert(sizeof(uid_t) == 4 && sizeof(mode_t) == 4, "scalar layout");
_Static_assert(sizeof(struct timeval) == 16 && (suseconds_t)-1 < 0, "timeval layout");
_Static_assert(sizeof(PRId64) == 4 && sizeof(PRIuPTR) == 2, "native formats");
const char* format = "%" PRId64 " %" PRIuPTR;
''', flags, suffix='.c')

includes = ['-I' + str(args.view / 'third_party/perfetto/include'),
            '-I' + str(args.generated / 'third_party/perfetto/build_config')]
identity = '''#include "perfetto/base/build_config.h"
#if !PERFETTO_BUILDFLAG(PERFETTO_OS_GTOS) || PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || PERFETTO_BUILDFLAG(PERFETTO_OS_WIN)
#error wrong GTOS identity
#endif
int native_identity;
'''
compile_case('perfetto-native-identity', identity, [*flags, *includes], suffix='.c')
compile_case('perfetto-dual-identity', identity, [*flags, *includes, '-D__linux__=1'],
             False, 'cannot inherit another operating system identity', suffix='.c')
linux = identity.replace('!PERFETTO_BUILDFLAG(PERFETTO_OS_GTOS) || PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX)',
                         'PERFETTO_BUILDFLAG(PERFETTO_OS_GTOS) || !PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX)')
compile_case('perfetto-linux-regression', linux,
             ['--target=x86_64-unknown-linux-gnu', '-ffreestanding', '-nostdinc', *includes], suffix='.c')
compile_case('perfetto-unknown-still-refuses', identity,
             ['--target=i686-unknown-none-elf', '-ffreestanding', '-nostdinc', *includes],
             False, 'OS not supported', suffix='.c')
assert before == {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in headers}
(args.out / 'results.json').write_text(json.dumps({'cases': results, 'source_before_after_identical': True,
    'scope': 'actual header/compiler contracts; no linking, threading runtime or guest claim'}, indent=2) + '\n')
print('CONTENT_SDK_COMPILER_CONTRACTS_PASS', len(results))
