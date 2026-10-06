#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
${CXX:-g++} -m32 -O2 -std=c++11 -ffreestanding -nostdlib -static -fno-builtin -fno-pie -no-pie \
 -fno-threadsafe-statics -fno-exceptions -fno-rtti -fno-stack-protector -Wall -Wextra -Werror -Iinclude \
 tests/storage_recovery_test.cpp tests/storage_runtime.cpp src/storage/appstore.cpp src/apps/package.cpp -o "$work/test"
if command -v qemu-i386 >/dev/null 2>&1; then qemu-i386 "$work/test"; else "$work/test"; fi
