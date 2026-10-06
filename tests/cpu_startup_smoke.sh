#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
kernel_flags=$(cat tools/kernel-cxxflags)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
qemu=${GTOS_QEMU_SYSTEM_I386:-qemu-system-i386}
if ! command -v "$qemu" >/dev/null 2>&1; then
    echo 'qemu-system-i386 is required (or set GTOS_QEMU_SYSTEM_I386)' >&2
    exit 77
fi
for source in src/gdt.cpp src/hardwarecommunication/cpu.cpp \
    src/hardwarecommunication/cpu_startup.cpp src/memory/physical.cpp \
    src/memory/bootstrap.cpp tests/cpu_startup_smoke.cpp; do
    ${CXX:-g++} $kernel_flags -m32 -std=c++11 -O2 -ffreestanding -nostdlib -fno-builtin \
        -fno-exceptions -fno-rtti -fno-stack-protector -fno-pie \
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-asynchronous-unwind-tables \
        -Iinclude -Wall -Wextra -Werror -c "$source" \
        -o "$work/$(basename "$source" .cpp).o"
done
as --32 src/hardwarecommunication/cpu_startup_trampoline.s -o "$work/trampoline.o"
as --32 tests/cpu_startup_loader.s -o "$work/loader.o"
ld -melf_i386 -T linker.ld -Map "$work/ap-smoke.bin.map" -o "$work/ap-smoke.bin" "$work"/*.o
python3 tools/audit-kernel-instructions.py --map "$work/ap-smoke.bin.map" "$work/ap-smoke.bin"
run() {
    name=$1; cpus=$2; memory=$3; arguments=$4; model=${5:-qemu32}
    set +e
    if [ -n "${GTOS_QEMU_DATA_DIR:-}" ]; then
        timeout 15 "$qemu" -L "$GTOS_QEMU_DATA_DIR" -machine pc -accel tcg \
            -cpu "$model" -m "$memory" -smp "$cpus" -kernel "$work/ap-smoke.bin" -append "$arguments" \
            -display none -monitor none -serial none -debugcon "file:$work/$name.log" \
            -device isa-debug-exit,iobase=0xf4,iosize=4 -no-reboot
    else
        timeout 15 "$qemu" -machine pc -accel tcg -cpu "$model" -m "$memory" -smp "$cpus" \
            -kernel "$work/ap-smoke.bin" -append "$arguments" \
            -display none -monitor none -serial none -debugcon "file:$work/$name.log" \
            -device isa-debug-exit,iobase=0xf4,iosize=4 -no-reboot
    fi
    status=$?
    set -e
    if [ "$status" -ne 33 ] || ! grep -q '^AP SMOKE PASS$' "$work/$name.log"; then
        cat "$work/$name.log"
        echo "FAIL: $name (QEMU exit $status)" >&2
        exit 1
    fi
    printf 'PASS: %s\n' "$name"
    if [ -n "${GTOS_AP_TEST_OUTPUT:-}" ]; then
        mkdir -p "$GTOS_AP_TEST_OUTPUT"
        cp "$work/$name.log" "$GTOS_AP_TEST_OUTPUT/$name.log"
    fi
}
run smp1-32m 1 32M n=1
run smp2-64m 2 64M n=2
run smp4-64m 4 64M n=4
run smp8-128m 8 128M n=8
run sparse-apic-ids '6,sockets=2,cores=3,threads=1' 64M n=6 max
run missing-ap-timeout 1 64M 'n=1 timeout'
run no-apic-rejection 2 64M 'n=2 reject'
