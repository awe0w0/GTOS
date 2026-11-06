#!/usr/bin/env python3
"""Build actual GTOS Rust module host tests; execution belongs to a second stage."""
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--module', required=True, choices=['error', 'tls', 'random', 'futex'])
parser.add_argument('--view', required=True, type=Path)
parser.add_argument('--clang-bin', required=True, type=Path)
parser.add_argument('--gcc13-root', required=True, type=Path)
parser.add_argument('--core-root', type=Path)
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=False)
repo = Path(__file__).resolve().parents[1]
test = repo / ('tests/gtos_rust_' + args.module + '_test.rs')
source = repo / ('apps/native_content_build/gtos-platform/rust/' + args.module + '.rs')
inputs = [test, source]
if args.module != 'error':
    if not args.core_root:
        parser.error('target compilation needs --core-root')
    inputs.extend([repo / 'apps/native_rust_target/i686-unknown-gtos.json',
                   args.core_root / 'libcore_core.rlib',
                   args.core_root / 'libcompiler_builtins_compiler_builtins.rlib'])
host_test = test
if args.module == 'futex':
    mutex = args.view / 'third_party/rust-toolchain/lib/rustlib/src/rust/library/std/src/sys/sync/mutex/futex.rs'
    condvar = args.view / 'third_party/rust-toolchain/lib/rustlib/src/rust/library/std/src/sys/sync/condvar/futex.rs'
    inputs.extend([mutex,condvar])
    host_test = args.out / 'host.rs'
    host_test.write_text('''#![allow(unsafe_op_in_unsafe_fn)]
pub use std::{hint,sync,time};
pub mod sys { pub mod sync { pub use crate::actual as futex; pub use crate::actual_mutex::Mutex; } }
''' + '#[path = ' + json.dumps(str(source)) + ']\npub mod actual;\n' +
        '#[path = ' + json.dumps(str(mutex)) + ']\npub mod actual_mutex;\n' +
        '#[path = ' + json.dumps(str(condvar)) + ']\npub mod actual_condvar;\n' +
        'include!(' + json.dumps(str(test)) + ');\n')
identities = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
images = []
targets = []
if args.module != 'error':
    prefix = '''#![no_std]
pub use core::{ffi,mem,ptr,sync};
macro_rules! rtabort { ($($message:tt)*) => { panic!($($message)*) }; }
'''
    if args.module == 'random':
        prefix = '#![no_std]\n'
        probe = '''#[unsafe(no_mangle)]
pub unsafe extern "C" fn gtos_random_probe(pointer: *mut u8, length: usize) {
    actual::fill_bytes(unsafe { core::slice::from_raw_parts_mut(pointer,length) });
}
'''
    elif args.module == 'tls':
        probe = '''#[unsafe(no_mangle)]
pub unsafe extern "C" fn gtos_tls_probe(key: usize, pointer: *mut u8, operation: u32,
    destructor: Option<unsafe extern "C" fn(*mut u8)>) -> usize {
    unsafe { match operation {
        0 => actual::create(destructor),
        1 => actual::get(key) as usize,
        2 => { actual::set(key,pointer); 0 },
        3 => { actual::destroy(key); 0 },
        _ => { static KEY: actual::LazyKey = actual::LazyKey::new(None); KEY.force() }
    } }
}
'''
    else:
        prefix = '#![no_std]\npub use core::{sync,time};\n'
        probe = '''#[unsafe(no_mangle)]
pub unsafe extern "C" fn gtos_futex_probe(pointer: *mut u32, operation: u32) -> bool {
    let value = unsafe { &*pointer.cast::<actual::Futex>() };
    match operation {
        0 => actual::futex_wait(value,1,Some(time::Duration::from_millis(1))),
        1 => actual::futex_wake(value),
        _ => { actual::futex_wake_all(value); true }
    }
}
'''
    (args.out / 'target.rs').write_text(prefix + '#[path = ' + json.dumps(str(source)) + ']\npub mod actual;\n' + probe)
for optimization in [0, 2]:
    image = args.out / ('rust-' + args.module + '-O' + str(optimization))
    result = subprocess.run([
        str(args.view / 'third_party/rust-toolchain/bin/rustc'), '--edition=2024', '--test', str(host_test),
        '-Dwarnings', '-Copt-level=' + str(optimization), '-Clinker=' + str(args.clang_bin / 'clang++'),
        '-Clink-arg=--sysroot=' + str(args.gcc13_root),
        '-Clink-arg=--gcc-install-dir=' + str(args.gcc13_root / 'usr/lib/gcc/x86_64-linux-gnu/13'),
        '-Clink-arg=-fuse-ld=lld', '-Clink-arg=-lstdc++', '-Clink-arg=-L' + str(args.gcc13_root / 'usr/lib/x86_64-linux-gnu'),
        '-Clink-arg=-Wl,-rpath-link,' + str(args.gcc13_root / 'usr/lib/x86_64-linux-gnu'), '-o', str(image)],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (args.out / ('build-O' + str(optimization) + '.log')).write_text(result.stdout)
    print(result.stdout, end='')
    if result.returncode:
        raise SystemExit(result.returncode)
    assert image.read_bytes()[:5] == b'\x7fELF\x02'
    image.chmod(0o555)
    images.append({'path': str(image), 'bytes': image.stat().st_size,
                   'sha256': hashlib.sha256(image.read_bytes()).hexdigest()})
    if args.module != 'error':
        target = args.out / (args.module + '-O' + str(optimization) + '.o')
        command = [str(args.view / 'third_party/rust-toolchain/bin/rustc'), '--edition=2024',
                   '-Zunstable-options', '--target', str(repo / 'apps/native_rust_target/i686-unknown-gtos.json'),
                   '--crate-type=rlib', '--emit=obj', '-Copt-level=' + str(optimization),
                   '--extern', 'core=' + str(args.core_root / 'libcore_core.rlib'),
                   '--extern', 'compiler_builtins=' + str(args.core_root / 'libcompiler_builtins_compiler_builtins.rlib'),
                   '-L', 'dependency=' + str(args.core_root), '-Dwarnings', str(args.out / 'target.rs'), '-o', str(target)]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (args.out / ('target-O' + str(optimization) + '.log')).write_text(result.stdout)
        print(result.stdout, end='')
        if result.returncode:
            raise SystemExit(result.returncode)
        data = target.read_bytes()
        assert data[:6] == b'\x7fELF\x01\x01' and struct.unpack_from('<HH', data, 16) == (1, 3)
        targets.append({'path': str(target), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
loader = args.out / 'ld-linux-x86-64.so.2'
shutil.copyfile(args.gcc13_root / 'usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2', loader)
loader.chmod(0o555)
assert identities == {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
with (args.out / 'manifest.json').open('x') as handle:
    json.dump({'images': images, 'sources': identities, 'target_objects': targets,
               'loader': {'path': str(loader), 'bytes': loader.stat().st_size,
                          'sha256': hashlib.sha256(loader.read_bytes()).hexdigest()},
               'library_path': str(args.gcc13_root / 'usr/lib/x86_64-linux-gnu')}, handle, indent=2)
print('RUST_MODULE_BUILD_PASS module=%s host_images=2 target_objects=%d' % (args.module, len(targets)))
