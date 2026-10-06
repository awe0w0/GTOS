#!/bin/sh
# Independent Multiboot/GRUB boots, including real supervisor page faults.
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
runtime="$(pwd)/../gtos-runtime"
qemu=${GTOS_QEMU_SYSTEM_I386:-qemu-system-i386}
grub=${GTOS_GRUB_MKRESCUE:-grub-mkrescue}
if [ -x "$runtime/root/usr/bin/qemu-system-i386" ] && [ "$qemu" = qemu-system-i386 ]; then
    qemu="$runtime/root/usr/bin/qemu-system-i386"
    export LD_LIBRARY_PATH="$runtime/root/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export QEMU_MODULE_DIR="$runtime/root/usr/lib/x86_64-linux-gnu/qemu"
    GTOS_QEMU_DATA_DIR="$runtime/root/usr/share/qemu"
fi
if ! command -v "$grub" >/dev/null 2>&1 && [ -x "$runtime/bin/grub-mkrescue" ]; then
    grub="$runtime/bin/grub-mkrescue"
fi
command -v "$qemu" >/dev/null 2>&1 || { echo 'qemu-system-i386 is required' >&2; exit 77; }
command -v "$grub" >/dev/null 2>&1 || { echo 'grub-mkrescue is required' >&2; exit 77; }
for source in src/gdt.cpp src/memory/paging.cpp src/memory/physical.cpp \
    src/memory/bootstrap.cpp tests/paging_smoke.cpp; do
    ${CXX:-g++} -m32 -std=c++11 -O2 -ffreestanding -nostdlib -fno-builtin \
        -fno-exceptions -fno-rtti -fno-stack-protector -fno-pie \
        -fno-threadsafe-statics -fno-use-cxa-atexit -fno-asynchronous-unwind-tables \
        -Iinclude -Wall -Wextra -Werror -c "$source" \
        -o "$work/$(basename "$source" .cpp).o"
done
as --32 tests/paging_smoke_loader.s -o "$work/loader.o"
ld -melf_i386 -T tests/paging_smoke.ld -o "$work/paging.bin" "$work"/*.o
mkdir -p "$work/iso/boot/grub"
cp "$work/paging.bin" "$work/iso/boot/paging.bin"
printf 'GTOS paging module test\n' > "$work/iso/boot/payload"
run() {
    caseName=$1; memory=$2; cpus=$3; name="$caseName-$memory-smp$cpus"
    printf 'set timeout=0\nset default=0\nmenuentry "Paging" {\n multiboot /boot/paging.bin case=%s\n module /boot/payload\n boot\n}\n' "$caseName" > "$work/iso/boot/grub/grub.cfg"
    "$grub" --output="$work/paging.iso" "$work/iso" > "$work/grub.log" 2>&1 || { cat "$work/grub.log"; exit 1; }
    set +e
    timeout 15 "$qemu" ${GTOS_QEMU_DATA_DIR:+-L "$GTOS_QEMU_DATA_DIR"} -machine pc -accel tcg \
        -m "$memory" -smp "$cpus" -cdrom "$work/paging.iso" -boot d -nic none \
        -display none -monitor none -serial none -debugcon "file:$work/$name.log" \
        -device isa-debug-exit,iobase=0xf4,iosize=4 -no-reboot > "$work/qemu.log" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 33 ] || ! grep -q '^PAGING SMOKE PASS$' "$work/$name.log"; then
        cat "$work/qemu.log" "$work/$name.log"
        echo "FAIL: $name (QEMU exit $status)" >&2
        exit 1
    fi
    printf 'PASS: %s\n' "$name"
    if [ -n "${GTOS_PAGING_TEST_OUTPUT:-}" ]; then
        mkdir -p "$GTOS_PAGING_TEST_OUTPUT"
        cp "$work/$name.log" "$GTOS_PAGING_TEST_OUTPUT/$name.log"
    fi
}
run positive 32M 1
run positive 64M 4
run positive 128M 8
for scenario in seal legacyflags pae null text rodata mmio unmap protect; do run "$scenario" 64M 4; done
