#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
. ./tests/i386-launcher.sh
launcher=$(i386_launcher_choose all)
kernel_flags=$(cat tools/kernel-cxxflags)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
for optimization in 0 2; do
    ${CXX:-g++} $kernel_flags -m32 -std=gnu++11 -O"$optimization" -ffreestanding -nostdlib \
        -fno-builtin -fno-exceptions -fno-rtti -fno-stack-protector -fno-pie -no-pie \
        -fno-threadsafe-statics -Wall -Wextra -Werror -Iinclude -Wl,-e,_start -Wl,--build-id=none \
        tests/gpt_test.cpp src/storage/gpt.cpp -o "$build/gpt"
    if ! i386_launcher_is_native "$launcher"; then
        "$(i386_launcher_path "$launcher")" "$build/gpt"
    elif command -v qemu-i386 >/dev/null 2>&1; then
        qemu-i386 "$build/gpt"
    else
        "$build/gpt"
    fi
done
