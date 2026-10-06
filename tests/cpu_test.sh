#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT HUP INT TERM
${CXX:-g++} -m32 -std=c++11 -DGTOS_CPU_TEST -Iinclude -Wall -Wextra -Werror \
    -fno-pie -no-pie -nostdlib -static -fno-stack-protector -fno-builtin \
    -fno-exceptions -fno-rtti -fno-use-cxa-atexit \
    tests/cpu_tests.cpp src/multitasking.cpp src/hardwarecommunication/cpu.cpp \
    -o "$work/cpu-tests"
if [ -n "${GTOS_QEMU_I386:-}" ]; then
    "$GTOS_QEMU_I386" "$work/cpu-tests"
elif command -v qemu-i386 >/dev/null 2>&1; then
    qemu-i386 "$work/cpu-tests"
else
    "$work/cpu-tests"
fi
