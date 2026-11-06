#!/usr/bin/env python3
"""Compile the real Dawn adapters with the GTOS SDK, without host headers."""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--view', required=True, type=Path)
parser.add_argument('--clang', required=True, type=Path)
parser.add_argument('--out', required=True, type=Path)
args = parser.parse_args()
args.out.mkdir(parents=True, exist_ok=False)
worktree = Path(__file__).resolve().parents[1]
sdk = worktree / 'apps/native_v8_runtime'
dawn = args.view / 'third_party/dawn'
sources = [dawn / 'src/dawn/common' / name for name in
           ['DynamicLib.cpp', 'SystemHandle.cpp', 'SystemUtils.cpp']]
sources += [dawn / 'src/utils/log.cc']
objects = []
for optimization in ['O0', 'O2']:
    for source in sources:
        output = args.out / (optimization + '-' + source.stem + '.o')
        command = [str(args.clang), '--target=i686-unknown-gtos', '-ffreestanding',
                   '-mno-sse', '-mno-sse2', '-fno-exceptions', '-fno-rtti',
                   '-std=c++20', '-' + optimization, '-D__GTOS__=1',
                   '-nostdinc', '-nostdinc++', '-I', str(dawn),
                   '-isystem', str(sdk / 'sdk'),
                   '-isystem', str(args.view / 'third_party/libc++/src/include'),
                   '-isystem', str(sdk / 'c-sdk'),
                   '-isystem', str(args.clang.parent.parent / 'lib/clang/24/include'),
                   '-I', str(worktree / 'apps/native_libxml_file/include'),
                   '-MD', '-MF', str(output.with_suffix('.d')), '-c', str(source), '-o', str(output)]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        output.with_suffix('.log').write_text(result.stdout)
        print(result.stdout, end='')
        if result.returncode:
            raise SystemExit(result.returncode)
        data = output.read_bytes()
        if data[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<HH', data, 16) != (1, 3):
            raise SystemExit('expected an ELF32 i386 relocatable object: ' + str(output))
        objects.append({'path': str(output), 'bytes': len(data),
                        'sha256': hashlib.sha256(data).hexdigest(),
                        'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest()})
with (args.out / 'manifest.json').open('x') as handle:
    json.dump(objects, handle, indent=2)
print('GTOS DAWN TARGET: %d ELF32 i386 objects with depfiles' % len(objects))
