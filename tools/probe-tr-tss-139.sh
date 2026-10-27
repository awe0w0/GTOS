#!/bin/bash
# GTOS-DSH-139 D: try to build tests/tr_tss_test.cpp, and record exactly how it fails.
#
# This is not a build step. It is the evidence for retiring the file, and it is written as a script rather than a claim so
# that the retirement rests on a reproducible result rather than on a reading of the source.
#
# The expectation, stated before the attempt: the file defines four of the nine host-seam symbols itself. src/gdt.cpp under
# GTOS_NATIVE_RUNTIME_HOST_TEST references all nine. So the file cannot link gdt.cpp (five symbols undefined) and cannot
# link tests/gdt_host_seam.cpp either (four symbols defined twice). Whichever way it is linked, it fails.
set -u

SRC="${1:?the worktree root}"
OUT="${2:?the output directory}"
mkdir -p "$OUT"
cd "$SRC"

FLAGS="-O2 -m32 -std=c++11 -Iinclude -Itests -ffreestanding -fno-use-cxa-atexit -nostdlib -fno-builtin \
-fno-rtti -fno-exceptions -fno-leading-underscore -fno-stack-protector -fno-pie \
-fno-asynchronous-unwind-tables -fcheck-new -Wno-write-strings -DGTOS_NATIVE_RUNTIME_HOST_TEST"

echo "=== does tr_tss_test.cpp compile on its own? ==="
g++ $FLAGS -c tests/tr_tss_test.cpp -o "$OUT/tr_tss_test.o" 2>&1 | head -20
echo "compile rc=$?"

echo "=== which host-seam symbols does it define? ==="
nm -g --defined-only "$OUT/tr_tss_test.o" 2>/dev/null | grep gtos_gdt_host | sed 's/^/  defined: /'

echo "=== which host-seam symbols does it leave undefined? ==="
nm -u "$OUT/tr_tss_test.o" 2>/dev/null | grep gtos_gdt_host | sed 's/^/  undefined: /'

g++ $FLAGS -c src/gdt.cpp -o "$OUT/gdt.o" 2>&1 | head -5
as --32 tests/tr_tss_start.s -o "$OUT/start.o" 2>&1 | head -5

echo "=== link attempt A: with gdt.cpp only, no seam unit ==="
# The exit code is taken from ld, not from the pipeline: `ld ... | head` reports head's status, which is always zero. That
# is the difference between "the linker failed and printed this" and "the linker succeeded", and getting it wrong would
# turn the whole probe into a false negative.
ld -melf_i386 -T tests/tr_tss.ld -o "$OUT/tr_tss_noseam.elf" \
  "$OUT/tr_tss_test.o" "$OUT/gdt.o" "$OUT/start.o" > "$OUT/link-a.log" 2>&1
LINK_A=$?
head -20 "$OUT/link-a.log"
echo "link A rc=$LINK_A"

echo "=== link attempt B: with gdt.cpp and the shared seam unit ==="
g++ $FLAGS -c tests/gdt_host_seam.cpp -o "$OUT/gdt_host_seam.o" 2>&1 | head -5
ld -melf_i386 -T tests/tr_tss.ld -o "$OUT/tr_tss_seam.elf" \
  "$OUT/tr_tss_test.o" "$OUT/gdt.o" "$OUT/gdt_host_seam.o" "$OUT/start.o" > "$OUT/link-b.log" 2>&1
LINK_B=$?
head -20 "$OUT/link-b.log"
echo "link B rc=$LINK_B"

echo "=== verdict ==="
if [ "$LINK_A" -ne 0 ] && [ "$LINK_B" -ne 0 ]; then
  echo "RETIRE: tr_tss_test.cpp cannot be linked with or without the shared seam unit"
else
  echo "KEEP: the file links, so retirement is not justified by this probe"
fi
