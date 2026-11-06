#!/usr/bin/env python3
"""Compile with the real GTOS GN toolchain and test system-header invalidation."""
import argparse
import hashlib
import struct
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--view', required=True, type=Path)
parser.add_argument('--gn', required=True)
parser.add_argument('--ninja', required=True)
parser.add_argument('--clang-bin', required=True, type=Path)
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
root = args.out
root.mkdir(parents=True, exist_ok=False)


def write(rel, text):
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def run(label, command, succeeds=True):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (root / (label + '.log')).write_text(result.stdout)
    assert (result.returncode == 0) == succeeds, result.stdout
    return result.stdout


def objects():
    paths = sorted((root / 'out/obj').rglob('*.o'))
    assert len(paths) == 2, paths
    for path in paths:
        data = path.read_bytes()
        assert data[:5] == b'\x7fELF\x01'
        assert struct.unpack_from('<HH', data, 16) == (1, 3), path
        deps = Path(str(path) + '.d').read_text()
        assert 'sdk/contract.h' in deps and 'local.h' in deps, deps
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}


write('.gn', 'buildconfig = "//config.gn"\n')
write('config.gn', 'set_default_toolchain("//build/toolchain/gtos:clang_x86")\n')
write('build/config/rust.gni', 'toolchain_has_rust = false\n')
write('build/toolchain/gtos/BUILD.gn',
      (args.view / 'build/toolchain/gtos/BUILD.gn').read_text())
write('BUILD.gn', '''static_library("sample") {
  sources = [ "sample_c.c", "sample_cpp.cc" ]
  cflags = [ "-isystem" + rebase_path("//sdk", root_build_dir) ]
}
group("default") { deps = [ ":sample" ] }
''')
write('local.h', '#define LOCAL_VALUE 5\n')
write('sdk/contract.h', '#define SDK_VALUE 17\n')
for filename, symbol in [('sample_c.c', 'c_value'), ('sample_cpp.cc', 'cpp_value')]:
    write(filename, '#include <contract.h>\n#include "local.h"\n'
          'int ' + symbol + '(void) { return SDK_VALUE + LOCAL_VALUE; }\n')
run('generate', [args.gn, 'gen', str(root / 'out'), '--root=' + str(root),
                 '--args=gtos_clang_bin="' + str(args.clang_bin) + '"'])
command = [args.ninja, '-C', str(root / 'out'), '-d', 'keepdepfile', 'obj/libsample.a']
run('initial', command)
before = objects()
print('PASS C and C++ produce ELF32 objects and complete SDK depfiles')
assert 'no work to do' in run('unchanged', command)
print('PASS unchanged headers do not rebuild')
write('sdk/contract.h', '#define SDK_VALUE 23\n')
run('sdk-change', command)
after = objects()
assert all(before[key] != after[key] for key in before)
print('PASS changing a system SDK header rebuilds both real objects')
write('sdk/contract.h', '#error deliberate SDK contract failure\n')
assert 'deliberate SDK contract failure' in run('invalid-sdk', command, False)
print('PASS an invalid changed SDK header fails the actual compiler')
write('sdk/contract.h', '#define SDK_VALUE 23\n')
run('recovery', command)
assert objects() == after
print('PASS corrected SDK rebuild recovers identical object bytes')
(root / 'sdk/contract.h').unlink()
assert 'contract.h' in run('missing-sdk', command, False)
print('PASS a missing SDK header cannot reuse the prior successful objects')
