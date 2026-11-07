#!/usr/bin/env python3
"""Compile real libtest consumers with the pinned host and GTOS sysroots."""
import argparse
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--rustc', required=True, type=Path)
parser.add_argument('--sysroot', required=True, type=Path)
parser.add_argument('--spec', required=True, type=Path)
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=False)

target_lib = args.sysroot / 'lib/rustlib/i686-unknown-gtos/lib/libtest_test.rlib'
assert target_lib.is_file(), target_lib


def compile_consumer(name, *, gtos, opt_in):
    source = args.out / (name + '.rs')
    output = args.out / (name + '.rmeta')
    source.write_text(
        '#![feature(test)]\n'
        + ('#![feature(restricted_std)]\n' if opt_in else '')
        + 'extern crate test;\n'
          'pub fn options() -> test::Options { test::Options::new() }\n'
          'pub fn format_result() -> std::fmt::Result { Ok(()) }\n'
    )
    command = [str(args.rustc), '--crate-type=rlib', '--emit=metadata',
               '-o', str(output), str(source)]
    if gtos:
        command += ['-Zunstable-options', '--target', str(args.spec),
                    '--sysroot', str(args.sysroot), '--extern',
                    'test=' + str(target_lib)]
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    (args.out / (name + '.log')).write_text(result.stdout)
    if gtos and not opt_in:
        assert result.returncode != 0 and 'E0658' in result.stdout, result.stdout
        assert 'restricted_std' in result.stdout and not output.exists(), result.stdout
    else:
        assert result.returncode == 0, result.stdout
        assert output.stat().st_size > 0


compile_consumer('gtos_opt_in', gtos=True, opt_in=True)
print('PASS GTOS libtest consumer compiles with explicit restricted_std opt-in')
compile_consumer('gtos_default', gtos=True, opt_in=False)
print('PASS GTOS std still rejects a consumer without its opt-in')
compile_consumer('host_default', gtos=False, opt_in=False)
print('PASS host libtest consumer requires no restricted_std opt-in')
