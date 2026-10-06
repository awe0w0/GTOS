#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
kernel_flags=$(cat tools/kernel-cxxflags)
qemu=${GTOS_QEMU_I386:-qemu-i386}
if ! command -v "$qemu" >/dev/null 2>&1 && [ -x ../gtos-runtime/root/usr/bin/qemu-i386 ]; then
    qemu="$(pwd)/../gtos-runtime/root/usr/bin/qemu-i386"
fi
for optimization in 0 2; do
    ${CXX:-g++} -m32 -std=c++11 -O"$optimization" $kernel_flags -Iinclude -Wall -Wextra -Werror \
        -fno-pie -no-pie -nostdlib -static -fno-stack-protector -fno-builtin \
        -fno-exceptions -fno-rtti -fno-use-cxa-atexit tests/native_fp_tests.cpp -o "$work/native-fp-tests"
    if command -v "$qemu" >/dev/null 2>&1; then "$qemu" "$work/native-fp-tests";
    else "$work/native-fp-tests"; fi
done
