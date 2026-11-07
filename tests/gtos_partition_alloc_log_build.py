#!/usr/bin/env python3
"""Build the real PartitionAlloc diagnostics with an injected write boundary."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--view', required=True, type=Path)
parser.add_argument('--generated', required=True, type=Path)
parser.add_argument('--clang', required=True)
parser.add_argument('--gcc13-root', required=True, type=Path)
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=False)
source = 'base/allocator/partition_allocator/src'
pa = args.view / source
headers = args.gcc13_root / 'usr/include'
libs = args.gcc13_root / 'usr/lib/x86_64-linux-gnu'
sources = [pa / 'partition_alloc/partition_alloc_base' / name for name in
           ('check.cc', 'logging.cc', 'log_message.cc', 'debug/alias.cc',
            'strings/cstring_builder.cc', 'strings/safe_sprintf.cc', 'strings/string_util.cc')]
sources.append(Path(__file__).with_name('gtos_partition_alloc_log_test.cpp'))


def identity(path):
    return {'path': str(path), 'bytes': path.stat().st_size,
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


images = []
for optimization in ('O0', 'O2'):
    image = args.out / ('partition-alloc-log-' + optimization)
    command = [args.clang, '-fuse-ld=lld', '-std=c++20', '-' + optimization,
               '-D__GTOS__=1', '-DPA_GTOS_LOG_TEST', '-Werror',
               '--sysroot=' + str(args.gcc13_root),
               '--gcc-install-dir=' + str(args.gcc13_root / 'usr/lib/gcc/x86_64-linux-gnu/13'),
               '-L' + str(libs), '-Wl,-rpath-link,' + str(libs),
               '-nostdinc++', '-isystem', str(headers / 'c++/13'),
               '-isystem', str(headers / 'x86_64-linux-gnu/c++/13'),
               '-I' + str(pa), '-I' + str(args.generated / source),
               '-idirafter' + str(args.view / 'build/config/gtos/include'),
               *map(str, sources), '-o', str(image)]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (args.out / (optimization + '.log')).write_text(result.stdout)
    print(result.stdout, end='')
    if result.returncode:
        raise SystemExit(result.returncode)
    image.chmod(0o555)
    images.append(identity(image))

loader = args.out / 'ld-linux-x86-64.so.2'
shutil.copyfile(libs / loader.name, loader)
loader.chmod(0o555)
with (args.out / 'manifest.json').open('x') as handle:
    json.dump({'images': images, 'loader': identity(loader), 'library_path': str(libs),
               'sources': list(map(identity, sources))}, handle, indent=2)
print('PARTITION_ALLOC_LOG_BUILD_PASS', len(images))
