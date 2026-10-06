#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
as --32 src/hardwarecommunication/cpu_startup_trampoline.s -o "$work/trampoline.o"
for optimization in 0 2; do
    ${CXX:-g++} -m32 -std=c++11 -O"$optimization" -DGTOS_MEMORY_TEST -DGTOS_CPU_STARTUP_TEST \
        -Iinclude -Wall -Wextra -Werror -fno-pie -no-pie -ffreestanding \
        -nostdlib -static -fno-stack-protector -fno-builtin -fno-exceptions \
        -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics \
        tests/cpu_startup_tests.cpp src/hardwarecommunication/cpu.cpp \
        src/hardwarecommunication/cpu_startup.cpp src/memory/physical.cpp \
        src/memory/bootstrap.cpp "$work/trampoline.o" -o "$work/ap-unit"
    if [ -n "${GTOS_QEMU_I386:-}" ]; then
        "$GTOS_QEMU_I386" "$work/ap-unit"
    elif command -v qemu-i386 >/dev/null 2>&1; then
        qemu-i386 "$work/ap-unit"
    else
        "$work/ap-unit"
    fi
done
