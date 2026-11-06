#!/usr/bin/env python3
"""Build real Dawn adapter tests; execution is a separate guarded stage."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--view', required=True, type=Path)
parser.add_argument('--clang', required=True)
parser.add_argument('--gcc13-root', required=True, type=Path)
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=False)
worktree = Path(__file__).resolve().parents[1]
dawn = args.view / 'third_party/dawn'
headers = args.gcc13_root / 'usr/include'
sources = [dawn / 'src/dawn/common' / name for name in
           ['DynamicLib.cpp', 'SystemHandle.cpp', 'SystemUtils.cpp']]
sources += [dawn / 'src/utils/log.cc', worktree / 'tests/gtos_dawn_platform_test.cpp']
images = []
for optimization in ['O0', 'O2']:
    image = args.out / ('dawn-gtos-' + optimization)
    command = [args.clang, '-fuse-ld=lld', '-std=c++20', '-' + optimization, '-D__GTOS__=1',
               '--sysroot=' + str(args.gcc13_root),
               '--gcc-install-dir=' + str(args.gcc13_root / 'usr/lib/gcc/x86_64-linux-gnu/13'),
               '-L' + str(args.gcc13_root / 'usr/lib/x86_64-linux-gnu'),
               '-Wl,-rpath-link,' + str(args.gcc13_root / 'usr/lib/x86_64-linux-gnu'),
               '-nostdinc++', '-isystem', str(headers / 'c++/13'),
               '-isystem', str(headers / 'x86_64-linux-gnu/c++/13'),
               '-I', str(dawn), '-Werror', '-Wno-self-move',
               *map(str, sources), '-o', str(image)]
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    (args.out / (optimization + '.log')).write_text(result.stdout)
    print(result.stdout, end='')
    if result.returncode:
        raise SystemExit(result.returncode)
    image.chmod(0o555)
    images.append({'path': str(image), 'bytes': image.stat().st_size,
                   'sha256': hashlib.sha256(image.read_bytes()).hexdigest()})
loader = args.out / 'ld-linux-x86-64.so.2'
shutil.copyfile(args.gcc13_root / 'usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2', loader)
loader.chmod(0o555)
with (args.out / 'manifest.json').open('x') as handle:
    json.dump({'images': images,
               'loader': {'path': str(loader), 'bytes': loader.stat().st_size,
                          'sha256': hashlib.sha256(loader.read_bytes()).hexdigest()},
               'library_path': str(args.gcc13_root / 'usr/lib/x86_64-linux-gnu'),
               'sources': [{'path': str(p), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
                           for p in sources]}, handle, indent=2)
print(json.dumps(images, indent=2))
