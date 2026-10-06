#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
kernel_flags=$(cat tools/kernel-cxxflags)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
qemu=${GTOS_QEMU_I386:-qemu-i386}
if ! command -v "$qemu" >/dev/null 2>&1 && [ -x ../gtos-runtime/root/usr/bin/qemu-i386 ]; then
    qemu="$(pwd)/../gtos-runtime/root/usr/bin/qemu-i386"
fi
for optimization in 0 2; do
    ${CXX:-g++} $kernel_flags -m32 -std=c++11 -O"$optimization" -DGTOS_CPU_TEST -Iinclude -Wall -Wextra -Werror \
        -fno-pie -no-pie -nostdlib -static -fno-stack-protector -fno-builtin \
        -fno-exceptions -fno-rtti -fno-use-cxa-atexit \
        tests/native_process_tests.cpp src/multitasking.cpp -o "$work/native-tests"
    if command -v "$qemu" >/dev/null 2>&1; then "$qemu" "$work/native-tests"; else "$work/native-tests"; fi
done
