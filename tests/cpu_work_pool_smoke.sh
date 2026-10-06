#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
repo=${GTOS_REPOSITORY:-$root}
stage=${GTOS_CPU_WORK_POOL_STAGE:-$root}
pool_source="$stage/src/hardwarecommunication"
if [ ! -f "$pool_source/cpu_work_pool.cpp" ]; then pool_source="$stage/src"; fi
cd "$repo"
kernel_flags=$(cat "$root/tools/kernel-cxxflags")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
qemu=${GTOS_QEMU_SYSTEM_I386:-qemu-system-i386}
for optimization in ${GTOS_CPU_WORK_POOL_OPTIMIZATIONS:-0 2}; do
    for source in src/gdt.cpp src/memorymanagement.cpp src/hardwarecommunication/cpu.cpp \
        src/memory/physical.cpp src/memory/bootstrap.cpp src/memory/paging.cpp \
        "$pool_source/cpu_startup.cpp" "$pool_source/cpu_work_pool.cpp" \
        "$pool_source/cpu_work_queue.cpp" "$pool_source/cpu_memory_types.cpp" \
        "$stage/tests/cpu_work_pool_smoke.cpp"; do
        ${CXX:-g++} $kernel_flags -m32 -std=c++11 -O"$optimization" -DGTOS_CPU_WORK_POOL_TEST \
            -ffreestanding -nostdlib -fno-builtin -fno-exceptions -fno-rtti \
            -fno-stack-protector -fno-pie -fno-use-cxa-atexit -fno-threadsafe-statics \
            -fno-asynchronous-unwind-tables -I"$stage/include" -Iinclude \
            -Wall -Wextra -Werror -c "$source" -o "$work/$(basename "$source" .cpp).o"
    done
    as --32 src/hardwarecommunication/cpu_startup_trampoline.s -o "$work/trampoline.o"
    as --32 "$pool_source/cpu_work_interrupts.s" -o "$work/interrupts.o"
    as --32 "$stage/tests/cpu_work_pool_loader.s" -o "$work/loader.o"
    ld -melf_i386 -T "$stage/tests/cpu_work_pool_smoke.ld" -Map "$work/work-smoke.bin.map" -o "$work/work-smoke.bin" "$work"/*.o
    python3 tools/audit-kernel-instructions.py --map "$work/work-smoke.bin.map" "$work/work-smoke.bin"
    run() {
        name=$1; cpus=$2; memory=$3; arguments=$4; model=${5:-qemu32}
        set +e
        if [ -n "${GTOS_QEMU_DATA_DIR:-}" ]; then
            timeout 20 "$qemu" -L "$GTOS_QEMU_DATA_DIR" -machine pc -accel tcg \
                -cpu "$model" -m "$memory" -smp "$cpus" -kernel "$work/work-smoke.bin" -append "$arguments" \
                -display none -monitor none -serial none -debugcon "file:$work/$name.log" \
                -device isa-debug-exit,iobase=0xf4,iosize=4 -no-reboot
        else
            timeout 20 "$qemu" -machine pc -accel tcg -cpu "$model" -m "$memory" -smp "$cpus" \
                -kernel "$work/work-smoke.bin" -append "$arguments" -display none -monitor none -serial none \
                -debugcon "file:$work/$name.log" -device isa-debug-exit,iobase=0xf4,iosize=4 -no-reboot
        fi
        status=$?
        set -e
        if [ "$status" -ne 33 ] || ! grep -q '^WORK POOL SMOKE PASS$' "$work/$name.log"; then
            cat "$work/$name.log"
            echo "FAIL: O$optimization $name (QEMU exit $status)" >&2
            exit 1
        fi
        printf 'PASS: O%s %s\n' "$optimization" "$name"
        if [ -n "${GTOS_CPU_WORK_POOL_OUTPUT:-}" ]; then
            mkdir -p "$GTOS_CPU_WORK_POOL_OUTPUT"
            cp "$work/$name.log" "$GTOS_CPU_WORK_POOL_OUTPUT/O$optimization-$name.log"
        fi
    }
    run smp1-32m 1 32M n=1
    run smp2-64m 2 64M n=2
    run smp4-64m 4 64M n=4
    run smp8-128m 8 128M n=8
    run sparse-apic-ids '6,sockets=2,cores=3,threads=1' 64M n=6
    run max-memory-compatibility 2 64M 'n=2 memory-compatibility' max
    run bad-checksum-rejection 2 64M 'n=2 bad-checksum'
    run no-mtrr 2 64M n=2 'qemu32,mtrr=off'
    run no-pat 2 64M n=2 'qemu32,pat=off'
done
