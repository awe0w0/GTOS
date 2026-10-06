#!/bin/sh
# Compiler-output tests only. No host execution of guest syscalls.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"$repo/tools/build-browser-probe.sh" "$work/first"
"$repo/tools/build-browser-probe.sh" "$work/second"
cmp "$work/first/browser-probe.elf" "$work/second/browser-probe.elf"
test -z "$(nm -u "$work/first/browser-probe.elf")"
if grep -Eq '\b(xmm[0-9]|mm[0-7]|fld|fstp|fadd|fsub|fmul|fdiv)\b' "$work/first/disassembly.txt"; then
    echo 'Unexpected FP/SIMD instruction in integer-only fixture' >&2
    exit 1
fi
python3 - "$work/first/inventory.json" <<'PY'
import json, sys
r = json.load(open(sys.argv[1]))
assert r['elf_bits'] == 32 and r['machine'] == 'i386'
assert r['elf_type'] == 'EXEC' and r['entry'] == 0x40000000
assert not r['interpreters']
loads = [s for s in r['segments'] if s['type'] == 'LOAD']
# Assert the compiler emitted the requested fixture, not that GTOS accepts it.
assert len(loads) == 2
assert [(s['address'], s['permissions']) for s in loads] == [(0x40000000, 'RX'), (0x40002000, 'RW')]
assert loads[0]['file_bytes'] > 0 and loads[0]['memory_bytes'] <= 4096
assert loads[1]['file_bytes'] == 4 and loads[1]['memory_bytes'] == 8
assert all(s['alignment'] == 4096 for s in loads)
assert [s['type'] for s in r['segments']] == ['LOAD', 'LOAD', 'GNU_STACK']
assert r['segments'][-1]['permissions'] == 'RW'
assert r['guest_loadability'] == 'not assessed'
PY
echo 'PASS browser probe: reproducible ELF32, defined symbols, intended layout, integer-only disassembly'
echo 'Guest execution, isolation and Chromium compatibility remain untested by this script'
