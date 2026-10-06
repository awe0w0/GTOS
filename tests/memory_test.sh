#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
kernel_flags=$(cat tools/kernel-cxxflags)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
# A static i386 Linux executable, without libc, libstdc++ or multilib headers.
# Only interrupt masking is disabled; the actual allocator sources are tested.
for optimization in 0 2; do
    ${CXX:-g++} $kernel_flags -m32 -std=gnu++11 -O"$optimization" -ffreestanding -nostdlib \
        -fno-builtin -fno-exceptions -fno-rtti -fno-stack-protector -fno-pie -no-pie \
        -fno-threadsafe-statics -fcheck-new -Wall -Wextra -Werror \
        -DGTOS_MEMORY_TEST -Iinclude -Wl,-e,_start -Wl,--build-id=none \
        tests/memory_test.cpp src/memorymanagement.cpp src/memory/physical.cpp \
        src/memory/selftest.cpp src/memory/bootstrap.cpp -o "$build/memory_test_O$optimization"
    if command -v qemu-i386 >/dev/null 2>&1; then
        qemu-i386 "$build/memory_test_O$optimization"
    else
        "$build/memory_test_O$optimization"
    fi
done

./tests/memory_bootstrap_test.sh
