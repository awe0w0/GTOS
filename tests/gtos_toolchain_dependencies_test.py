#!/usr/bin/env python3
"""Compile with the real GTOS GN toolchain and test system-header invalidation."""
import argparse
import hashlib
import re
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


def objects(expected=2):
    paths = sorted((root / 'out/obj').rglob('*.o'))
    assert len(paths) == expected, paths
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
compiler_config = (args.view / 'build/config/compiler/BUILD.gn').read_text()
split = re.search(r'^config\("split_dwarf"\) \{\n.*?^\}\n', compiler_config, re.M | re.S)
assert split, 'the actual split_dwarf config is missing'
write('BUILD.gn', '''is_chromeos = false
use_thin_lto = false
''' + split.group(0) + '''
static_library("sample") {
  sources = [ "sample_c.c", "sample_cpp.cc" ]
  cflags = [ "-isystem" + rebase_path("//sdk", root_build_dir) ]
}
static_library("split") {
  sources = [ "sample_c.c", "sample_cpp.cc" ]
  configs = [ ":split_dwarf" ]
  cflags = [ "-g", "-isystem" + rebase_path("//sdk", root_build_dir) ]
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
write('sdk/contract.h', '#define SDK_VALUE 23\n')
run('baseline-before-split', command)
split_command = [*command[:-1], 'obj/libsplit.a']
run('split-initial', split_command)
all_objects = objects(4)
debug_files = sorted((root / 'out/obj').rglob('*.dwo'))
assert len(debug_files) == 2, debug_files
split_objects = [path for path in (root / 'out/obj').rglob('*.o') if 'split' in path.parts]
assert len(split_objects) == 2
assert set(debug_files) == {path.with_suffix('.dwo') for path in split_objects}, debug_files
for path in debug_files:
    data = path.read_bytes()
    assert data[:5] == b'\x7fELF\x01' and struct.unpack_from('<HH', data, 16) == (1, 3)
print('PASS actual split_dwarf config builds both object/debug pairs with valid SDK depfiles')
assert 'no work to do' in run('split-unchanged', split_command)
print('PASS split debug outputs match Ninja paths and remain up to date')
debug_files[0].unlink()
output = run('split-missing-debug', split_command)
assert debug_files[0].is_file() and 'GTOS C' in output
assert objects(4) == all_objects
print('PASS a missing debug output triggers real recompilation and identical recovery')
