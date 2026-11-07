#!/usr/bin/env python3
"""Compile a consumer of the actual GTOS FilePath rlib and check its gates."""

import argparse
from pathlib import Path
import subprocess


parser = argparse.ArgumentParser()
for name in ('rustc', 'build', 'spec', 'out'):
    parser.add_argument('--' + name, required=True, type=Path)
args = parser.parse_args()
args.out.mkdir(exist_ok=False)

base = args.build / 'obj/base'
flags = (base / 'file_path.rsp.rust').read_text().splitlines()
dependencies = [flag for flag in flags if flag.startswith('-Ldependency=')]
allowed = '-Zallow-features=gtos_ext,restricted_std'
assert [flag for flag in flags if flag.startswith('-Zallow-features=')] == [allowed]
library = base / 'libbase_file_path.rlib'
command = [str(args.rustc), '-Zunstable-options', '--target', str(args.spec),
           '--sysroot', str(args.build / 'local_rustc_sysroot'), '--edition=2024',
           '--crate-type=rlib', '--emit=metadata', *dependencies,
           '--extern', 'base_file_path=' + str(library), allowed]

source = """
#![feature(gtos_ext, restricted_std)]
use base_file_path::ffi::FilePath;
use std::ffi::OsStr;
use std::os::gtos::{OsStrExt, OsStringExt};
use std::path::Path;

pub fn verify_native_path(bytes: &[u8]) -> bool {
    let borrowed = OsStr::from_bytes(bytes);
    let native = FilePath::from_path(Path::new(borrowed));
    native.as_os_str().as_bytes() == bytes &&
        native.as_path() == Path::new(borrowed) &&
        native.to_path_buf().into_os_string().into_vec() == bytes
}
"""
for name, code, diagnostic in [
    ('byte_path_consumer', source, None),
    ('native_extension_requires_opt_in', source.replace('gtos_ext, ', ''), 'E0658'),
    ('unrelated_unstable_feature', source.replace('gtos_ext, ', 'gtos_ext, linkage, '), 'E0725'),
]:
    input_file = args.out / (name + '.rs')
    output = args.out / (name + '.rmeta')
    input_file.write_text(code)
    result = subprocess.run(command + ['-o', str(output), str(input_file)],
                            cwd=args.build, capture_output=True, text=True)
    log = result.stdout + result.stderr
    (args.out / (name + '.log')).write_text(log)
    if diagnostic is None:
        assert result.returncode == 0 and output.stat().st_size > 0, log
    else:
        assert result.returncode != 0 and diagnostic in log and not output.exists(), log
    print('PASS', name)
