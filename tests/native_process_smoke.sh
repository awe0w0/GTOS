#!/bin/sh
# Real CPL3/CR3/IRQ/syscall isolation boots, at O0 and O2.
set -eu
cd "$(dirname "$0")/.."
kernel_flags=$(cat tools/kernel-cxxflags)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
runtime="$(pwd)/../gtos-runtime"
qemu=${GTOS_QEMU_SYSTEM_I386:-qemu-system-i386}
qemu64=${GTOS_QEMU_SYSTEM_X86_64:-qemu-system-x86_64}
grub=${GTOS_GRUB_MKRESCUE:-grub-mkrescue}
if [ -x "$runtime/root/usr/bin/qemu-system-i386" ] && [ "$qemu" = qemu-system-i386 ]; then
    qemu="$runtime/root/usr/bin/qemu-system-i386"
    export LD_LIBRARY_PATH="$runtime/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export QEMU_MODULE_DIR="$runtime/root/usr/lib/x86_64-linux-gnu/qemu"
    GTOS_QEMU_DATA_DIR="$runtime/root/usr/share/qemu"
fi
if [ -x "$runtime/root/usr/bin/qemu-system-x86_64" ] && [ "$qemu64" = qemu-system-x86_64 ]; then
    qemu64="$runtime/root/usr/bin/qemu-system-x86_64"
fi
if ! command -v "$grub" >/dev/null 2>&1 && [ -x "$runtime/bin/grub-mkrescue" ]; then
    grub="$runtime/bin/grub-mkrescue"
fi
command -v "$qemu" >/dev/null 2>&1 || { echo 'qemu-system-i386 is required' >&2; exit 77; }
command -v "$qemu64" >/dev/null 2>&1 || { echo 'qemu-system-x86_64 is required for the SCE feature proof' >&2; exit 77; }
command -v "$grub" >/dev/null 2>&1 || { echo 'grub-mkrescue is required' >&2; exit 77; }
as --32 tests/native_process_elf.s -o "$work/elf-fixture.tmp"
ld -melf_i386 -T tests/native_process_elf.ld -o "$work/fixture.elf" "$work/elf-fixture.tmp"
for optimization in 0 2; do
for source in src/gdt.cpp src/multitasking.cpp src/syscalls.cpp src/hardwarecommunication/interrupts.cpp src/hardwarecommunication/port.cpp src/process/native_runtime.cpp src/process/native_realtime.cpp src/process/resources.cpp src/process/native_surface.cpp src/process/native_fp.cpp src/process/elf32.cpp src/memory/process_address_space.cpp src/memory/paging.cpp src/memory/physical.cpp \
    src/memory/bootstrap.cpp tests/native_process_smoke.cpp; do
    ${CXX:-g++} $kernel_flags -m32 -std=c++11 -O"$optimization" -ffreestanding -nostdlib -fno-builtin \
        -fno-exceptions -fno-rtti -fno-stack-protector -fno-pie \
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-asynchronous-unwind-tables \
        -Iinclude -Wno-write-strings -Wall -Wextra -Werror -c "$source" \
        -o "$work/$(basename "$source" .cpp).o"
done
as --32 tests/native_process_loader.s -o "$work/loader.o"
as --32 tests/native_process_user.s -o "$work/user.o"
as --32 src/process/native_fp.s -o "$work/native_fp.asm.o"
as --32 src/hardwarecommunication/interruptstubs.s -o "$work/stubs.o"
ld -melf_i386 -T tests/native_process_smoke.ld -Map "$work/native.bin.map" -o "$work/native.bin" "$work"/*.o
python3 tools/audit-kernel-instructions.py --map "$work/native.bin.map" \
    --allow-user-range native_user_start:native_user_end "$work/native.bin"
mkdir -p "$work/iso/boot/grub"
cp "$work/native.bin" "$work/iso/boot/native.bin"
cp "$work/fixture.elf" "$work/iso/boot/payload"
run() {
    caseName=$1; memory=$2; cpus=$3; name="O$optimization-$caseName-$memory-smp$cpus"
    printf 'set timeout=0\nset default=0\nmenuentry "Native isolation" {\n multiboot /boot/native.bin case=%s\n module /boot/payload\n boot\n}\n' "$caseName" > "$work/iso/boot/grub/grub.cfg"
    "$grub" --output="$work/native.iso" "$work/iso" > "$work/grub.log" 2>&1 || { cat "$work/grub.log"; exit 1; }
    cpuOption=""; selectedQemu="$qemu"
    # The guest stays i386. QEMU's i386-only engine masks SYSCALL capability;
    # use the x86_64 engine's qemu64 CPU to exercise real EFER.SCE support.
    if [ "$caseName" = sce ]; then cpuOption="-cpu qemu64"; selectedQemu="$qemu64"; fi
    set +e
    timeout 40 "$selectedQemu" ${GTOS_QEMU_DATA_DIR:+-L "$GTOS_QEMU_DATA_DIR"} -machine pc -accel tcg $cpuOption \
        -m "$memory" -smp "$cpus" -cdrom "$work/native.iso" -boot d -nic none \
        -display none -monitor none -serial none -debugcon "file:$work/$name.log" \
        -device isa-debug-exit,iobase=0xf4,iosize=4 -no-reboot > "$work/qemu.log" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 33 ] || ! grep -q '^NATIVE PROCESS SMOKE PASS$' "$work/$name.log"; then
        cat "$work/qemu.log" "$work/$name.log"
        echo "FAIL: $name (QEMU exit $status)" >&2
        exit 1
    fi
    printf 'PASS: %s\n' "$name"
    if [ -n "${GTOS_NATIVE_TEST_OUTPUT:-}" ]; then
        mkdir -p "$GTOS_NATIVE_TEST_OUTPUT"
        cp "$work/$name.log" "$GTOS_NATIVE_TEST_OUTPUT/$name.log"
    fi
}
run isolation 32M 1
run isolation 64M 4
run isolation 128M 1
run osfxsr 64M 4
run sce 64M 4
done
