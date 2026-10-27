#!/bin/bash
# GTOS-DSH-139 H: build and run every host unit that links src/gdt.cpp, so a change to the GDT class layout is proved
# against all of its users rather than only against the one unit the change was written for.
#
# This exists because the change in this packet is a member addition plus a method addition on GlobalDescriptorTable. That
# changes sizeof() and the constructor's initialiser list, and every unit that constructs the object is affected. The
# kernel build proves the production path; the units here prove the host paths. A unit that silently stopped compiling would
# otherwise only surface when the driver reached it much later, with the failure attributed to whatever phase happened to
# run it.
#
# Each unit is built the way its own linker script and start file expect, then run directly: these are 32-bit Linux programs
# that write through the 0x80 gate, not kernels.
set -u

SRC="${1:?the worktree root}"
OUT="${2:?the output directory}"
mkdir -p "$OUT"
cd "$SRC"

FLAGS="-O2 -m32 -std=c++11 -Iinclude -Itests -ffreestanding -fno-use-cxa-atexit -nostdlib -fno-builtin \
-fno-rtti -fno-exceptions -fno-leading-underscore -fno-stack-protector -fno-pie \
-fno-asynchronous-unwind-tables -fcheck-new -Wno-write-strings -DGTOS_NATIVE_RUNTIME_HOST_TEST"

fail=0
report() { printf '%-28s %s\n' "$1" "$2"; }

# The safe-TSS unit: gdt.cpp plus the shared seam.
build_run() {
  local name="$1"; shift
  local ld="$1"; shift
  local start="$1"; shift
  local extra="$*"
  local dir="$OUT/$name"
  mkdir -p "$dir"
  local objs=""
  if ! g++ $FLAGS -c tests/"$name".cpp -o "$dir/main.o" 2>"$dir/build.log"; then
    report "$name" "BUILD FAIL (compile)"; head -5 "$dir/build.log" | sed 's/^/    /'; fail=1; return
  fi
  objs="$dir/main.o"
  for f in $extra; do
    local o="$dir/$(basename "$f" .cpp).o"
    if ! g++ $FLAGS -c "$f" -o "$o" 2>>"$dir/build.log"; then
      report "$name" "BUILD FAIL ($f)"; tail -5 "$dir/build.log" | sed 's/^/    /'; fail=1; return
    fi
    objs="$objs $o"
  done
  if [ -n "$start" ]; then
    as --32 tests/"$start" -o "$dir/start.o" 2>>"$dir/build.log" || { report "$name" "ASSEMBLE FAIL"; fail=1; return; }
    objs="$dir/start.o $objs"
  fi
  if ! ld -melf_i386 -T tests/"$ld" -o "$dir/unit.elf" $objs 2>>"$dir/build.log"; then
    report "$name" "LINK FAIL"; tail -8 "$dir/build.log" | sed 's/^/    /'; fail=1; return
  fi
  "$dir/unit.elf" >"$dir/stdout.log" 2>"$dir/run.log"
  local rc=$?
  local pass
  pass=$(grep -c 'PASS' "$dir/run.log" "$dir/stdout.log" 2>/dev/null | awk -F: '{s+=$2} END {print s+0}')
  if [ "$rc" -eq 0 ] && [ "$pass" -gt 0 ]; then
    report "$name" "PASS rc=0 pass_lines=$pass"
  else
    report "$name" "RUN FAIL rc=$rc pass_lines=$pass"
    tail -8 "$dir/run.log" | sed 's/^/    /'
    fail=1
  fi
}

build_run safe_tss_test safe_tss.ld safe_tss_start.s src/gdt.cpp tests/gdt_host_seam.cpp

echo "---"
if [ "$fail" -eq 0 ]; then echo "ALL GDT HOST UNITS PASS"; else echo "GDT HOST UNITS FAILED"; fi
exit "$fail"
