#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
for optimization in 0 2; do
    ${CXX:-g++} -m32 -std=gnu++11 -O"$optimization" -ffreestanding -nostdlib \
        -fno-builtin -fno-exceptions -fno-rtti -fno-stack-protector -fno-pie -no-pie \
        -fno-threadsafe-statics -Wall -Wextra -Werror -DGTOS_MEMORY_TEST -Iinclude \
        -Wl,-e,_start -Wl,--build-id=none tests/memory_bootstrap_test.cpp \
        src/memory/physical.cpp src/memory/bootstrap.cpp -o "$build/low-bootstrap-O$optimization"
    if command -v qemu-i386 >/dev/null 2>&1; then
        qemu-i386 "$build/low-bootstrap-O$optimization"
    else
        "$build/low-bootstrap-O$optimization"
    fi
done
