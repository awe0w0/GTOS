"""Compile consumers of the actual Content rlibs and bound their feature access."""
import argparse
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--rustc', required=True, type=Path)
parser.add_argument('--build', required=True, type=Path)
parser.add_argument('--spec', required=True, type=Path)
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
args.out.mkdir(exist_ok=False)

crates = {'serde': 'serde/v1', 'serde_core': 'serde_core/v1',
          'foldhash': 'foldhash/v0_2', 'memchr': 'memchr/v2'}
root = args.build / 'obj/third_party/rust'
libs = {name: root / rel / ('lib/lib%s_lib.rlib' % name) for name, rel in crates.items()}
response = [(root / rel / 'lib.rsp.rust').read_text().splitlines() for rel in crates.values()]
allowlist = '-Zallow-features=restricted_std'
assert all([item for item in lines if item.startswith('-Zallow-features=')] == [allowlist]
           for lines in response)
deps = sorted({item for lines in response for item in lines if item.startswith('-Ldependency=')})
severity = args.build / 'obj/base/logging/liblog_severity_bindgen_5c984a66.rlib'
libs['severity'] = severity
assert all(path.is_file() and path.stat().st_size > 0 for path in libs.values())
command = [str(args.rustc), '-Zunstable-options', '--target', str(args.spec),
           '--sysroot', str(args.build / 'local_rustc_sysroot'),
           '--edition=2024', '--crate-type=rlib', '--emit=metadata', *deps]
for name, path in libs.items():
    command += ['--extern', name + '=' + str(path)]


def compile_consumer(name, source, diagnostic=None):
    path = args.out / (name + '.rs')
    path.write_text(source)
    output = args.out / (name + '.rmeta')
    result = subprocess.run(command + [allowlist, '-o', str(output), str(path)],
                            cwd=args.build, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    (args.out / (name + '.log')).write_text(result.stdout)
    if diagnostic is None:
        assert result.returncode == 0 and output.stat().st_size > 0, result.stdout
    else:
        assert result.returncode != 0 and diagnostic in result.stdout, result.stdout
        assert not output.exists()
    print('PASS ' + name)


code = """
#![no_std]
use core::hash::BuildHasher;
use serde::{Serialize, de::DeserializeOwned};
use severity::root::logging::LogSeverity;
fn serializable<T: Serialize + DeserializeOwned>() {}
pub fn use_content_libraries(bytes: &[u8], severity: LogSeverity) -> Option<usize> {
    serializable::<(u32, bool)>();
    let _hash = foldhash::fast::RandomState::default().hash_one(severity);
    memchr::memchr(severity as u8, bytes)
}
"""
compile_consumer('native_content_libraries', code)
compile_consumer('wrong_bindgen_type',
                 code.replace('let _hash =', 'let _: &str = severity; let _hash ='), 'E0308')
compile_consumer('unapproved_unstable_feature', '#![feature(linkage)]\n' + code, 'E0725')
compile_consumer('default_std_is_restricted', code.replace('#![no_std]', ''), 'restricted_std')
compile_consumer('native_os_string_serialization_is_unsupported',
                 code.replace('#![no_std]', '#![feature(restricted_std)]')
                     .replace('(u32, bool)', 'std::ffi::OsString'), 'E0277')
