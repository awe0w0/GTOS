#!/bin/bash
# GTOS-DSH-139 H/D: build and run the safe-TSS host unit against the production observation.
#
# The unit is a freestanding 32-bit ELF that issues write(2) through the 0x80 gate, so it is not run by the host kernel
# directly; it is loaded by the loader in tests/safe_tss.ld under the same emulator the guest scenes use. That is the shape
# every host unit in this repository has, and the point of running it here rather than only compiling it is that a unit that
# compiles and does not pass is not evidence.
#
# What this unit can and cannot settle is stated in the test itself: it cannot execute `ltr`, so the busy-bit obligation is
# modelled through the seam rather than observed. The guest scene observes it.
set -eu

SRC="${1:?the worktree root}"
OUT="${2:?the output directory}"
mkdir -p "$OUT"

FLAGS="-O2 -m32 -std=c++11 -Iinclude -Itests -ffreestanding -fno-use-cxa-atexit -nostdlib -fno-builtin \
-fno-rtti -fno-exceptions -fno-leading-underscore -fno-stack-protector -fno-pie \
-fno-asynchronous-unwind-tables -fcheck-new -Wno-write-strings -DGTOS_NATIVE_RUNTIME_HOST_TEST"

cd "$SRC"
g++ $FLAGS -c tests/safe_tss_test.cpp -o "$OUT/safe_tss_test.o"
g++ $FLAGS -c src/gdt.cpp -o "$OUT/gdt.o"
g++ $FLAGS -c tests/gdt_host_seam.cpp -o "$OUT/gdt_host_seam.o"
as --32 tests/safe_tss_start.s -o "$OUT/start.o"
ld -melf_i386 -T tests/safe_tss.ld -o "$OUT/safe_tss.elf" \
  "$OUT/safe_tss_test.o" "$OUT/gdt.o" "$OUT/gdt_host_seam.o" "$OUT/start.o"

# The unit is a 32-bit Linux program, not a kernel: its linker script loads it at 0x08048000 and it writes through the
# 0x80 gate, which is this host's own syscall entry. Running it under the emulator is wrong for that reason -- QEMU's
# -kernel wants a bootable image and refuses a plain ELF, which is exactly what the first attempt here reported. It runs
# directly, on the host, which is also the only place `int $0x80` means what the unit thinks it means.
set +e
"$OUT/safe_tss.elf" >"$OUT/safe_tss.stdout.log" 2>"$OUT/safe_tss.e9.log"
RC=$?
set -e
echo "safe_tss rc=$RC out=$(wc -c < "$OUT/safe_tss.e9.log")"
