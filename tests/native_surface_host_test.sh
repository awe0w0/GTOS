#!/bin/bash
# New bounded surface service only; real checked-copy/display require QEMU.
set -euo pipefail
if [ "$#" -ne 1 ]; then
  echo "usage: native_surface_host_test.sh FRESH_ARTIFACT_DIR" >&2
  exit 2
fi
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$1"
if [ -e "$out" ]; then
  echo "Refusing to reuse artifact directory: $out" >&2
  exit 2
fi
mkdir -p "$out"
out="$(realpath "$out")"
compiler="${CXX:-g++}"
"$compiler" --version > "$out/compiler.txt"
printf 'CXX=%q bash %q %q\n' "$compiler" "$repo/tests/native_surface_host_test.sh" "${out}-reproduced" > "$out/reproduce.sh"
sha256sum "$repo/include/process/surface_abi.h" "$repo/include/process/native_surface.h" \
  "$repo/include/gui/native_image.h" "$repo/include/memory/criticalsection.h" \
  "$repo/src/process/native_surface.cpp" "$repo/tests/native_surface_host.cpp" \
  "$repo/tests/native_surface_host_test.sh" > "$out/source-hashes.txt"
for level in 0 2; do
  "$compiler" -std=c++11 -O"$level" -g -Wall -Wextra -Werror -pedantic \
    -DGTOS_SURFACE_HOST_TEST -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
    -I"$repo/include" "$repo/src/process/native_surface.cpp" "$repo/tests/native_surface_host.cpp" \
    -o "$out/native-surface-host-x64-O$level" > "$out/build-x64-O$level.log" 2>&1
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    "$out/native-surface-host-x64-O$level" > "$out/run-x64-O$level.log" 2>&1
  cat "$out/run-x64-O$level.log"
  "$compiler" -std=c++11 -O"$level" -g -Wall -Wextra -Werror -pedantic -m32 \
    -DGTOS_SURFACE_HOST_TEST -DGTOS_SURFACE_I386_HOST -ffreestanding -fno-builtin \
    -fno-rtti -fno-exceptions -fno-use-cxa-atexit -fno-stack-protector -fno-pie -no-pie \
    -mno-sse -mno-mmx -msoft-float -nostdlib -Wl,-e,_start \
    -I"$repo/include" "$repo/src/process/native_surface.cpp" "$repo/tests/native_surface_host.cpp" \
    -o "$out/native-surface-host-i386-O$level" > "$out/build-i386-O$level.log" 2>&1
  readelf -h "$out/native-surface-host-i386-O$level" > "$out/readelf-i386-O$level.log"
  "$out/native-surface-host-i386-O$level" > "$out/run-i386-O$level.log" 2>&1
  cat "$out/run-i386-O$level.log"
done
# Compile actual production i386 guards, without the host-only substitutions.
"$compiler" -std=c++11 -O2 -m32 -ffreestanding -fno-builtin -fno-rtti -fno-exceptions \
  -fno-use-cxa-atexit -fno-stack-protector -fno-pie -fno-asynchronous-unwind-tables \
  -mno-sse -mno-mmx -msoft-float -I"$repo/include" -c "$repo/src/process/native_surface.cpp" \
  -o "$out/native-surface-production-i386.o" > "$out/build-production-i386.log" 2>&1
nm -u -C "$out/native-surface-production-i386.o" > "$out/production-undefined-symbols.txt"
objdump -d -C "$out/native-surface-production-i386.o" > "$out/production-disassembly.txt"
sha256sum "$out/native-surface-host-"*"-O"[02] "$out/native-surface-production-i386.o" > "$out/binary-hashes.txt"
python3 - "$repo" "$out" "$compiler" <<'PY'
import json
from pathlib import Path
import re
import sys
repo, out, compiler = sys.argv[1:]
out = Path(out)
undefined = (out / 'production-undefined-symbols.txt').read_text()
assert not re.search(r'operator new|operator delete|\b(?:malloc|calloc|realloc|free)\b', undefined), undefined
sections = {}
name = None
for line in (out / 'production-disassembly.txt').read_text().splitlines():
    label = re.match(r'^[0-9a-f]+ <(.+)>:$', line)
    if label:
        name = label.group(1)
        sections[name] = []
    elif name is not None:
        sections[name].append(line)
guarded = ['SetAvailable', 'Begin', 'ValidateWrite', 'Write', 'Present', 'Abort', 'ReclaimOwner', 'CopyLatest']
for method in guarded:
    bodies = [body for name, body in sections.items()
              if 'gtos::process::NativeSurfaceBank::' + method + '(' in name]
    assert len(bodies) == 1, (method, len(bodies))
    body = '\n'.join(bodies[0])
    assert re.search(r'\bcli\b', body) and re.search(r'\bsti\b', body), method
logs = {}
for architecture, bits in [('x64', 64), ('i386', 32)]:
    for level in ['O0', 'O2']:
        key = architecture + '-' + level
        logs[key] = (out / ('run-' + key + '.log')).read_text().strip()
        assert 'NATIVE SURFACE HOST PASS' in logs[key] and 'pointer_bits=' + str(bits) in logs[key]
        if architecture == 'i386':
            elf = (out / ('native-surface-host-i386-' + level)).read_bytes()
            assert elf[:6] == b'\x7fELF\x01\x01' and int.from_bytes(elf[18:20], 'little') == 3
status = dict(all_required_checks_pass=True, source=repo, compiler=compiler,
    scope='Actual pure surface service; native checked-copy, BSP admission and GUI closing require real guest acceptance',
    optimizations=['O0', 'O2'], x64_sanitizers=['address', 'undefined'],
    i386_execution='Actual Linux ELF32, same test/service code, freestanding int80 report/exit, no missing multilib CRT',
    i386_sanitizers=[], production_guard_methods=guarded, production_no_heap_imports=True,
    logs=logs)
(out / 'status.json').write_text(json.dumps(status, indent=2) + '\n')
print('Evidence:', out / 'status.json')
PY
