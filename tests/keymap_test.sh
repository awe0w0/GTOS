#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
${CXX:-g++} -m32 -O2 -std=c++11 -nostdlib -static -ffreestanding -fno-builtin -fno-pie -no-pie \
  -fno-stack-protector -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Iinclude \
  tests/keymap_test.cpp src/drivers/keymap.cpp -o "$work/keys"
if command -v qemu-i386 >/dev/null 2>&1; then qemu-i386 "$work/keys"; else "$work/keys"; fi
