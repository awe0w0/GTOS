#!/usr/bin/env python3
"""Compile consumers of the two actual allocator bindings in the GTOS sysroot."""
import argparse
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--rustc', required=True, type=Path)
parser.add_argument('--build', required=True, type=Path)
parser.add_argument('--spec', required=True, type=Path)
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=False)

allocators = args.build / 'obj/build/rust/allocator'
sources = """
#![no_std]
use allocator_impls_ffi::root::rust_allocator_internal as alloc;
use alloc_error_handler_impl_ffi::root::rust_allocator_internal as error;
const _: unsafe extern "C" fn(usize, usize) -> *mut u8 = alloc::alloc;
const _: unsafe extern "C" fn(*mut u8, usize, usize) = alloc::dealloc;
const _: unsafe extern "C" fn(*mut u8, usize, usize, usize) -> *mut u8 = alloc::realloc;
const _: unsafe extern "C" fn(usize, usize) -> *mut u8 = alloc::alloc_zeroed;
const _: unsafe extern "C" fn() = error::alloc_error_handler_impl;
"""


def compile_consumer(name, code, diagnostic=None):
    source = args.out / (name + '.rs')
    output = args.out / (name + '.rmeta')
    source.write_text(code)
    command = [str(args.rustc), '-Zunstable-options', '--target', str(args.spec),
               '--sysroot', str(args.build / 'local_rustc_sysroot'),
               '--edition=2024', '--crate-type=rlib', '--emit=metadata']
    for library in ('allocator_impls_ffi', 'alloc_error_handler_impl_ffi'):
        command += ['--extern', library + '=' + str(allocators / ('lib' + library + '.rlib'))]
    result = subprocess.run(command + ['-o', str(output), str(source)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (args.out / (name + '.log')).write_text(result.stdout)
    if diagnostic is None:
        assert result.returncode == 0 and output.stat().st_size > 0, result.stdout
    else:
        assert result.returncode != 0 and diagnostic in result.stdout, result.stdout
        assert not output.exists()
    print('PASS ' + name)


compile_consumer('actual_core_only_signatures', sources)
compile_consumer('wrong_pointer_signature',
                 sources.replace('-> *mut u8 = alloc::alloc;', '-> usize = alloc::alloc;'),
                 'E0308')
compile_consumer('std_policy_retained', sources.replace('#![no_std]', ''), 'restricted_std')
