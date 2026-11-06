#!/usr/bin/env python3
"""Build the actual Rust allocator tests and compile its GTOS object; do not run them here."""
import argparse
import hashlib
import json
import shutil
import struct
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--view', required=True, type=Path)
parser.add_argument('--clang-bin', required=True, type=Path)
parser.add_argument('--gcc13-root', required=True, type=Path)
parser.add_argument('--core-root', required=True, type=Path)
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=False)
repo = Path(__file__).resolve().parents[1]
backend = repo / 'apps/native_content_build/gtos-platform/rust/alloc.rs'
cases = repo / 'tests/gtos_rust_allocator_test.rs'
parent = args.view / 'third_party/rust-toolchain/lib/rustlib/src/rust/library/std/src/sys/alloc/mod.rs'
spec = repo / 'apps/native_rust_target/i686-unknown-gtos.json'
rustc = args.view / 'third_party/rust-toolchain/bin/rustc'
inputs = [backend, cases, parent, spec]
identities = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}

# Use the upstream parent's real MIN_ALIGN and realloc_fallback, not a model.
prefix = parent.read_text().split('cfg_select! {', 1)[0]
assert 'unsafe fn realloc_fallback(' in prefix and 'ptr::copy_nonoverlapping' in prefix
(args.out / 'actual.rs').write_text(prefix + '\n#[path = ' + json.dumps(str(backend)) + ']\nmod gtos;\n'
                                  'pub use gtos::{alloc, alloc_zeroed, dealloc, realloc};\n')
(args.out / 'host.rs').write_text('pub use std::{alloc,ffi,ptr};\nmod actual;\n'
                                'include!(' + json.dumps(str(cases)) + ');\n')
(args.out / 'target.rs').write_text('''#![no_std]
pub mod alloc { pub use core::alloc::Layout; }
pub use core::{ffi,ptr};
mod actual;
#[unsafe(no_mangle)]
pub unsafe extern "C" fn gtos_rust_allocator_probe(p: *mut u8, size: usize, align: usize, operation: u32) -> *mut u8 {
    let Ok(layout) = alloc::Layout::from_size_align(size,align) else { return ptr::null_mut(); };
    unsafe { match operation {
        0 => actual::alloc(layout),
        1 => actual::alloc_zeroed(layout),
        2 => actual::realloc(p,layout,size),
        _ => { actual::dealloc(p,layout); ptr::null_mut() }
    } }
}
''')


def run(name, command):
    result = subprocess.run(list(map(str, command)), text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (args.out / (name + '.log')).write_text(result.stdout)
    print(result.stdout, end='')
    if result.returncode:
        raise SystemExit(result.returncode)


images = []
for optimization in [0, 2]:
    image = args.out / ('rust-allocator-O' + str(optimization))
    run('host-O' + str(optimization), [rustc, '--edition=2024', '--test', args.out / 'host.rs', '-Dwarnings',
        '-Copt-level=' + str(optimization), '-Clinker=' + str(args.clang_bin / 'clang++'),
        '-Clink-arg=--sysroot=' + str(args.gcc13_root),
        '-Clink-arg=--gcc-install-dir=' + str(args.gcc13_root / 'usr/lib/gcc/x86_64-linux-gnu/13'),
        '-Clink-arg=-fuse-ld=lld', '-Clink-arg=-L' + str(args.gcc13_root / 'usr/lib/x86_64-linux-gnu'),
        '-Clink-arg=-Wl,-rpath-link,' + str(args.gcc13_root / 'usr/lib/x86_64-linux-gnu'), '-o', image])
    image.chmod(0o555)
    images.append({'path': str(image), 'bytes': image.stat().st_size, 'sha256': hashlib.sha256(image.read_bytes()).hexdigest()})
    target = args.out / ('allocator-O' + str(optimization) + '.o')
    run('target-O' + str(optimization), [rustc, '--edition=2024', '-Zunstable-options', '--target', spec,
        '--crate-type=rlib', '--emit=obj', '-Copt-level=' + str(optimization),
        '--extern', 'core=' + str(args.core_root / 'libcore_core.rlib'),
        '--extern', 'compiler_builtins=' + str(args.core_root / 'libcompiler_builtins_compiler_builtins.rlib'),
        '-L', 'dependency=' + str(args.core_root), '-Dwarnings', args.out / 'target.rs', '-o', target])
    data = target.read_bytes()
    assert data[:6] == b'\x7fELF\x01\x01' and struct.unpack_from('<HH', data, 16) == (1, 3), target
loader = args.out / 'ld-linux-x86-64.so.2'
shutil.copyfile(args.gcc13_root / 'usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2', loader)
loader.chmod(0o555)
assert identities == {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
with (args.out / 'manifest.json').open('x') as handle:
    json.dump({'images': images, 'sources': identities,
               'loader': {'path': str(loader), 'bytes': loader.stat().st_size, 'sha256': hashlib.sha256(loader.read_bytes()).hexdigest()},
               'library_path': str(args.gcc13_root / 'usr/lib/x86_64-linux-gnu')}, handle, indent=2)
print('RUST_ALLOCATOR_BUILD_PASS host_images=2 target_elf32_objects=2')
