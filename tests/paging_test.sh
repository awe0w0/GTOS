#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
for optimization in 0 2; do
    ${CXX:-g++} -m32 -std=gnu++11 -O"$optimization" -ffreestanding -nostdlib \
        -fno-builtin -fno-exceptions -fno-rtti -fno-stack-protector -fno-pie -no-pie \
        -fno-threadsafe-statics -Wall -Wextra -Werror \
        -DGTOS_MEMORY_TEST -DGTOS_PAGING_TEST -Iinclude -Wl,-e,_start -Wl,--build-id=none \
        tests/paging_test.cpp src/memory/paging.cpp src/memory/physical.cpp \
        src/memory/bootstrap.cpp -o "$build/paging_test_O$optimization"
    if command -v qemu-i386 >/dev/null 2>&1; then
        qemu-i386 "$build/paging_test_O$optimization"
    elif [ -x ../gtos-runtime/root/usr/bin/qemu-i386 ]; then
        LD_LIBRARY_PATH=../gtos-runtime/root/usr/lib/x86_64-linux-gnu \
            ../gtos-runtime/root/usr/bin/qemu-i386 "$build/paging_test_O$optimization"
    else
        "$build/paging_test_O$optimization"
    fi
done
